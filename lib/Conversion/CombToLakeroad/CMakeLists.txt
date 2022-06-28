add_circt_library(CIRCTCombToLakeroad
  CombToLakeroad.cpp

  ADDITIONAL_HEADER_DIRS
  ${MLIR_MAIN_INCLUDE_DIR}/mlir/Conversion/CombToLakeroad

  DEPENDS
  CIRCTConversionPassIncGen

  LINK_COMPONENTS
  Core

  LINK_LIBS PUBLIC
  CIRCTCalyx
  CIRCTSV
  CIRCTSeq
  CIRCTHW
  MLIRIR
  MLIRPass
  MLIRFuncDialect
  MLIRSupport
  MLIRTransforms
  )
