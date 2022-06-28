//===- CombToLakeroad.cpp - Translate Comb into Lakeroad-------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

//===----------------------------------------------------------------------===//
//
//
//
//===----------------------------------------------------------------------===//

#include "circt/Conversion/CombToLakeroad.h"
#include "../PassDetail.h"
#include "circt/Dialect/Comb/CombDialect.h"
#include "circt/Dialect/Comb/CombOps.h"
#include "circt/Dialect/HW/HWDialect.h"
#include "circt/Dialect/HW/HWOps.h"
#include "circt/Dialect/HW/HWTypes.h"
#include "circt/Dialect/SV/SVDialect.h"
#include "circt/Dialect/SV/SVOps.h"
#include "circt/Dialect/Seq/SeqOps.h"
#include "mlir/IR/ImplicitLocOpBuilder.h"
#include "mlir/IR/TypeUtilities.h"
#include "mlir/Transforms/DialectConversion.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/FormatVariadic.h"

using namespace mlir;
using namespace circt;
using namespace circt::calyx;
using namespace circt::comb;
using namespace circt::hw;
using namespace circt::seq;
using namespace circt::sv;

Operation *
getOrCreate(std::string opName, int width, long unsigned int arity,
            std::map<std::tuple<std::string, int, int>, Operation *> *map,
            Operation *parentModule, MLIRContext *ctx) {

  auto key = std::tuple(opName, width, arity);

  if (map->find(key) == map->end()) {
    OpBuilder moduleBuilder(parentModule);
    llvm::SmallVector<PortInfo> ports(
        {{StringAttr::get(ctx, "out"), hw::PortDirection::OUTPUT,
          hw::IntType::get(Builder(ctx).getI32IntegerAttr(width))}});

    for (long unsigned int i = 0; i < arity; ++i) {
      ports.push_back(
          {StringAttr::get(ctx, llvm::formatv("i{0}", i)),
           hw::PortDirection::INPUT,
           hw::IntType::get(Builder(ctx).getI32IntegerAttr(width))});
    }

    map->insert(
        std::pair(key, moduleBuilder.create<HWModuleExternOp>(
                           parentModule->getLoc(),
                           moduleBuilder.getStringAttr(llvm::formatv(
                               "lakeroad_{0}_{1}_{2}", opName, width, arity)),
                           ports)));
  }

  return map->at(key);
}
struct OrOpConversion : public OpConversionPattern<comb::OrOp> {
  OrOpConversion(MLIRContext *context,
                 std::map<std::tuple<std::string, int, int>, Operation *> *map)
      : OpConversionPattern(context), map(map) {}

  LogicalResult
  matchAndRewrite(comb::OrOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto width = getElementTypeOrSelf(op.getResult()).getIntOrFloatBitWidth();
    auto arity = adaptor.getOperands().size();

    auto mod = getOrCreate("or", width, arity, map,
                           op->getParentOfType<HWModuleOp>(), getContext());
    rewriter.replaceOpWithNewOp<hw::InstanceOp>(
        op, mod, StringAttr::get(getContext(), "lakeroad_generated_instance"),
        llvm::to_vector(op.getOperands()));

    return success();
  }

private:
  std::map<std::tuple<std::string, int, int>, Operation *> *map;
};
struct AndOpConversion : public OpConversionPattern<comb::AndOp> {
  AndOpConversion(MLIRContext *context,
                  std::map<std::tuple<std::string, int, int>, Operation *> *map)
      : OpConversionPattern(context), map(map) {}

  LogicalResult
  matchAndRewrite(comb::AndOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto width = getElementTypeOrSelf(op.getResult()).getIntOrFloatBitWidth();
    auto arity = adaptor.getOperands().size();

    auto mod = getOrCreate("and", width, arity, map,
                           op->getParentOfType<HWModuleOp>(), getContext());
    rewriter.replaceOpWithNewOp<hw::InstanceOp>(
        op, mod, StringAttr::get(getContext(), "lakeroad_generated_instance"),
        llvm::to_vector(op.getOperands()));

    return success();
  }

private:
  std::map<std::tuple<std::string, int, int>, Operation *> *map;
};

struct XorOpConversion : public OpConversionPattern<comb::XorOp> {
  XorOpConversion(MLIRContext *context,
                  std::map<std::tuple<std::string, int, int>, Operation *> *map)
      : OpConversionPattern(context), map(map) {}

  LogicalResult
  matchAndRewrite(comb::XorOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto width = getElementTypeOrSelf(op.getResult()).getIntOrFloatBitWidth();
    auto arity = adaptor.getOperands().size();

    auto mod = getOrCreate("xor", width, arity, map,
                           op->getParentOfType<HWModuleOp>(), getContext());
    rewriter.replaceOpWithNewOp<hw::InstanceOp>(
        op, mod, StringAttr::get(getContext(), "lakeroad_generated_instance"),
        llvm::to_vector(op.getOperands()));

    return success();
  }

private:
  std::map<std::tuple<std::string, int, int>, Operation *> *map;
};

/// Pass entrypoint.

namespace {
class CombToLakeroadPass : public CombToLakeroadBase<CombToLakeroadPass> {
public:
  void runOnOperation() override;
};
} // end anonymous namespace

void CombToLakeroadPass::runOnOperation() {
  auto *ctx = &getContext();

  ModuleOp module = getOperation();
  OpBuilder moduleBuilder(module.getBodyRegion());

  std::map<std::tuple<std::string, int, int>, Operation *> map;

  ConversionTarget target(*ctx);
  target.addIllegalOp<comb::XorOp>();
  target.addLegalOp<hw::InstanceOp>();
  RewritePatternSet patterns(ctx);
  patterns.add<XorOpConversion>(ctx, &map);
  patterns.add<AndOpConversion>(ctx, &map);
  patterns.add<OrOpConversion>(ctx, &map);
  if (applyPartialConversion(getOperation(), target, std::move(patterns))
          .failed()) {
    signalPassFailure();
    return;
  }
}

std::unique_ptr<mlir::Pass> circt::createCombToLakeroadPass() {
  return std::make_unique<CombToLakeroadPass>();
}
