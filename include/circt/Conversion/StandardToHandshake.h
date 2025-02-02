//===- StandardToHandshake.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares passes which together will lower the Standard dialect to
// Handshake dialect.
//
//===----------------------------------------------------------------------===//

#ifndef CIRCT_CONVERSION_STANDARDTOHANDSHAKE_H_
#define CIRCT_CONVERSION_STANDARDTOHANDSHAKE_H_

#include "circt/Dialect/Handshake/HandshakeOps.h"
#include "circt/Dialect/Handshake/HandshakePasses.h"
#include "mlir/Transforms/DialectConversion.h"
#include <memory>

namespace mlir {
class ModuleOp;
template <typename T>
class OperationPass;
} // namespace mlir

namespace circt {

namespace handshake {

// ============================================================================
// Partial lowering infrastructure
// ============================================================================

using RegionLoweringFunc =
    llvm::function_ref<LogicalResult(Region &, ConversionPatternRewriter &)>;
// Convenience function for executing a PartialLowerRegion with a provided
// partial lowering function.
LogicalResult partiallyLowerRegion(const RegionLoweringFunc &loweringFunc,
                                   MLIRContext *ctx, Region &r);

// Holds the shared state of the different transformations required to transform
// Standard to Handshake operations. The transformations are expressed as member
// functions to simplify access to the state.
//
// This class' member functions expect a rewriter that matched on the parent
// operation of the encapsulated region.
class HandshakeLowering {
public:
  using BlockValues = DenseMap<Block *, std::vector<Value>>;
  using BlockOps = DenseMap<Block *, std::vector<Operation *>>;
  using blockArgPairs = DenseMap<Value, Operation *>;
  using MemRefToMemoryAccessOp =
      llvm::MapVector<Value, std::vector<Operation *>>;

  explicit HandshakeLowering(Region &r) : r(r) {}
  LogicalResult addMergeOps(ConversionPatternRewriter &rewriter);
  LogicalResult addBranchOps(ConversionPatternRewriter &rewriter);
  LogicalResult replaceCallOps(ConversionPatternRewriter &rewriter);

  template <typename TTerm>
  LogicalResult setControlOnlyPath(ConversionPatternRewriter &rewriter) {
    // Creates start and end points of the control-only path

    // Temporary start node (removed in later steps) in entry block
    Block *entryBlock = &r.front();
    rewriter.setInsertionPointToStart(entryBlock);
    Operation *startOp = rewriter.create<StartOp>(entryBlock->front().getLoc());
    setBlockEntryControl(entryBlock, startOp->getResult(0));

    // Replace original return ops with new returns with additional control
    // input
    for (auto retOp : llvm::make_early_inc_range(r.getOps<TTerm>())) {
      rewriter.setInsertionPoint(retOp);
      SmallVector<Value, 8> operands(retOp->getOperands());
      operands.push_back(startOp->getResult(0));
      rewriter.replaceOpWithNewOp<handshake::ReturnOp>(retOp, operands);
    }
    return success();
  }
  LogicalResult connectConstantsToControl(ConversionPatternRewriter &rewriter,
                                          bool sourceConstants);

  LogicalResult loopNetworkRewriting(ConversionPatternRewriter &rewriter);

  BlockOps insertMergeOps(BlockValues blockLiveIns, blockArgPairs &mergePairs,
                          ConversionPatternRewriter &rewriter);

  // Insert appropriate type of Merge CMerge for control-only path,
  // Merge for single-successor blocks, Mux otherwise
  Operation *insertMerge(Block *block, Value val,
                         ConversionPatternRewriter &rewriter);

  // Replaces standard memory ops with their handshake version (i.e.,
  // ops which connect to memory/LSQ). Returns a map with an ordered
  // list of new ops corresponding to each memref. Later, we instantiate
  // a memory node for each memref and connect it to its load/store ops
  LogicalResult replaceMemoryOps(ConversionPatternRewriter &rewriter,
                                 MemRefToMemoryAccessOp &memRefOps);

  LogicalResult connectToMemory(ConversionPatternRewriter &rewriter,
                                MemRefToMemoryAccessOp memRefOps, bool lsq);
  void setMemOpControlInputs(ConversionPatternRewriter &rewriter,
                             ArrayRef<Operation *> memOps, Operation *memOp,
                             int offset, ArrayRef<int> cntrlInd);

  LogicalResult finalize(ConversionPatternRewriter &rewriter);

  // Returns the entry control value for operations contained within this
  // block.
  Value getBlockEntryControl(Block *block) const;

  void setBlockEntryControl(Block *block, Value v);

  Region &getRegion() { return r; }
  MLIRContext *getContext() { return r.getContext(); }

protected:
  Region &r;

private:
  DenseMap<Block *, Value> blockEntryControlMap;
};

// Driver for the HandshakeLowering class.
// Note: using two different vararg template names due to potantial references
// that would cause a type mismatch
template <typename T, typename... TArgs, typename... TArgs2>
LogicalResult runPartialLowering(
    T &instance,
    LogicalResult (T::*memberFunc)(ConversionPatternRewriter &, TArgs2...),
    TArgs &...args) {
  return partiallyLowerRegion(
      [&](Region &, ConversionPatternRewriter &rewriter) -> LogicalResult {
        return (instance.*memberFunc)(rewriter, args...);
      },
      instance.getContext(), instance.getRegion());
}

// Helper to check the validity of the dataflow conversion
LogicalResult checkDataflowConversion(Region &r, bool disableTaskPipelining);

// Driver that applies the partial lowerings expressed in HandshakeLowering to
// the region encapsulated in it. The region is assumed to have a terminator of
// type TTerm. See HandshakeLowering for the different lowering steps.
template <typename TTerm>
LogicalResult lowerRegion(HandshakeLowering &hl, bool sourceConstants,
                          bool disableTaskPipelining) {
  //  Perform initial dataflow conversion. This process allows for the use of
  //  non-deterministic merge-like operations.
  HandshakeLowering::MemRefToMemoryAccessOp memOps;

  if (failed(
          runPartialLowering(hl, &HandshakeLowering::replaceMemoryOps, memOps)))
    return failure();
  if (failed(runPartialLowering(hl,
                                &HandshakeLowering::setControlOnlyPath<TTerm>)))
    return failure();
  if (failed(runPartialLowering(hl, &HandshakeLowering::addMergeOps)))
    return failure();
  if (failed(runPartialLowering(hl, &HandshakeLowering::replaceCallOps)))
    return failure();
  if (failed(runPartialLowering(hl, &HandshakeLowering::addBranchOps)))
    return failure();

  // The following passes modifies the dataflow graph to being safe for task
  // pipelining. In doing so, non-deterministic merge structures are replaced
  // for deterministic structures.
  if (!disableTaskPipelining) {
    if (failed(
            runPartialLowering(hl, &HandshakeLowering::loopNetworkRewriting)))
      return failure();
  }

  // Fork/sink materialization. @todo: this should be removed and
  // materialization should be run as a separate pass afterward initial dataflow
  // conversion! However, connectToMemory has some hard-coded assumptions on the
  // existence of fork/sink operations...
  if (failed(partiallyLowerRegion(addSinkOps, hl.getContext(), hl.getRegion())))
    return failure();

  if (failed(runPartialLowering(
          hl, &HandshakeLowering::connectConstantsToControl, sourceConstants)))
    return failure();

  if (failed(partiallyLowerRegion(addForkOps, hl.getContext(), hl.getRegion())))
    return failure();
  if (failed(checkDataflowConversion(hl.getRegion(), disableTaskPipelining)))
    return failure();

  bool lsq = false;
  if (failed(runPartialLowering(hl, &HandshakeLowering::connectToMemory, memOps,
                                lsq)))
    return failure();

  // Add  control argument to entry block, replace references to the
  // temporary handshake::StartOp operation, and finally remove the start
  // op.
  if (failed(runPartialLowering(hl, &HandshakeLowering::finalize)))
    return failure();
  return success();
}

/// Remove basic blocks inside the given region. This allows the result to be
/// a valid graph region, since multi-basic block regions are not allowed to
/// be graph regions currently.
void removeBasicBlocks(Region &r);

} // namespace handshake

std::unique_ptr<mlir::OperationPass<mlir::ModuleOp>>
createHandshakeAnalysisPass();

std::unique_ptr<mlir::OperationPass<mlir::ModuleOp>>
createStandardToHandshakePass();

std::unique_ptr<mlir::OperationPass<handshake::FuncOp>>
createHandshakeCanonicalizePass();

std::unique_ptr<mlir::OperationPass<handshake::FuncOp>>
createHandshakeRemoveBlockPass();

} // namespace circt

#endif // CIRCT_CONVERSION_STANDARDTOHANDSHAKE_H_
