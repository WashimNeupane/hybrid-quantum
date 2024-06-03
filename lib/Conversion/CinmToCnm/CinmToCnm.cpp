
#include "cinm-mlir/Dialect/Cnm/IR/CnmTypes.h"
#include <cinm-mlir/Conversion/CinmPasses.h>
#include <cinm-mlir/Dialect/Cnm/IR/CnmOps.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallVector.h>
#include <memory>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/Linalg/IR/Linalg.h>
#include <mlir/Dialect/Tensor/IR/Tensor.h>
#include <mlir/IR/AffineExpr.h>
#include <mlir/IR/AffineMap.h>
#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/BuiltinTypes.h>
#include <mlir/IR/IRMapping.h>
#include <mlir/IR/ImplicitLocOpBuilder.h>
#include <mlir/IR/Location.h>
#include <mlir/IR/PatternMatch.h>
#include <mlir/IR/TypeRange.h>
#include <mlir/IR/ValueRange.h>
#include <mlir/Support/LogicalResult.h>
#include <mlir/Transforms/DialectConversion.h>
#include <numeric>
#include <optional>

using namespace mlir;
#define GEN_PASS_CLASSES
#include <cinm-mlir/Conversion/CinmPasses.h.inc>

namespace {

void computeShapeOfTensors(llvm::ArrayRef<int64_t> shape,
                           cnm::WorkgroupType wgTy,
                           llvm::SmallVectorImpl<int64_t> &shapeOfTensor,
                           llvm::SmallVectorImpl<int64_t> &shapeOfBuffer) {
  auto wgShape = wgTy.getShape();

  auto numWgItems =
      std::reduce(wgShape.begin(), wgShape.end(), 1, std::multiplies<>());
  auto numBufItems =
      std::reduce(shape.begin(), shape.end(), 1, std::multiplies<>());
  assert(numBufItems % numWgItems == 0);
  auto remainder = numBufItems / numWgItems;

  shapeOfTensor.append(wgShape.begin(), wgShape.end());
  if (remainder != 1) {
    // tensor is WG shape + remainder
    shapeOfTensor.push_back(remainder);
    // buffer is just remainder, WG dimensions are implicit
    shapeOfBuffer.push_back(remainder);
  }
}

AffineMap computeAffineMap(cnm::WorkgroupType wgTy) {
  auto wgShape =
      mlir::getAffineConstantExprs(wgTy.getShape(), wgTy.getContext());

  SmallVector<AffineExpr, 4> dimExprs;
  dimExprs.reserve(wgShape.size() * 2);
  for (size_t i = 0; i < wgShape.size(); i++) {
    auto index = mlir::getAffineDimExpr(i, wgTy.getContext());
    dimExprs.emplace_back(index.floorDiv(wgShape[i]));
  }
  for (size_t i = 0; i < wgShape.size(); i++) {
    auto index = mlir::getAffineDimExpr(i, wgTy.getContext());
    dimExprs.emplace_back(index % wgShape[i]);
  }
  return AffineMap::get(dimExprs.size(), 0, dimExprs, wgTy.getContext());
}

Value convertInputIntoAlloc(Value inputBuf, Value workGroup,
                            cnm::WorkgroupType wgTy, AffineMap &scatterMap,
                            ImplicitLocOpBuilder &rewriter) {
  // For each input of the reduce, we need to

  auto inputTy = inputBuf.getType().cast<RankedTensorType>();
  llvm::SmallVector<int64_t, 4> shapeOfTensor;
  llvm::SmallVector<int64_t, 1> shapeOfBuffer;
  computeShapeOfTensors(inputTy.getShape(), wgTy, shapeOfTensor, shapeOfBuffer);
  cnm::BufferType bufTy =
      cnm::BufferType::get(rewriter.getContext(), shapeOfBuffer,
                           inputTy.getElementType(), wgTy.getShape(),
                           0); // todo level is hardcoded

  // Reshape original tensor
  Value shapeReified = rewriter.create<arith::ConstantOp>(
      rewriter.getI64TensorAttr(shapeOfTensor));
  Value reshaped = rewriter.create<tensor::ReshapeOp>(
      inputTy.cloneWith(shapeOfTensor, inputTy.getElementType()), inputBuf,
      shapeReified);

  // Allocate a cinm buffer
  Value alloc = rewriter.create<cnm::AllocOp>(bufTy, workGroup);

  // Scatter into buffer
  scatterMap = computeAffineMap(wgTy);
  rewriter.create<cnm::ScatterOp>(reshaped, alloc, workGroup, scatterMap);

  return alloc;
}

llvm::SmallVector<Value, 1>
convertLinalgReduceIntoLaunch(ImplicitLocOpBuilder builder,
                              linalg::ReduceOp reduction,
                              linalg::ReduceOp::Adaptor adaptor,
                              Value workgroup, cnm::WorkgroupType wgTy) {

  llvm::SmallVector<Value, 3> launchOperands;
  llvm::SmallVector<AffineMap, 3> affineMaps;
  llvm::SmallVector<Type, 3> mappedArgTypes;

  builder.setInsertionPointAfter(reduction);
  for (auto input : adaptor.getOperands()) {
    launchOperands.emplace_back(convertInputIntoAlloc(
        input, workgroup, wgTy, affineMaps.emplace_back(), builder));
  }

  cnm::LaunchOp launchOp =
      builder.create<cnm::LaunchOp>(workgroup, launchOperands);

  {
    auto &launchBlock = launchOp.getBody().emplaceBlock();
    // arguments are memrefs with same shape as inputs
    for (auto input : launchOp.getParams()) {
      if (auto inputTy = input.getType().cast<cnm::BufferType>()) {
        auto mappedTy =
            MemRefType::get(inputTy.getShape(), inputTy.getElementType());
        launchBlock.addArgument(mappedTy, input.getLoc());
      } else {
        launchBlock.addArgument(input.getType(), input.getLoc());
      }
    }
    auto args = launchBlock.getArguments();
    auto firstOutput = args.begin() + reduction.getNumDpsInputs();
    llvm::SmallVector<Value, 2> reduceInpts(args.begin(), firstOutput);
    llvm::SmallVector<Value, 1> reduceInits(firstOutput, args.end());

    // Here we are copying the original reduce into the launch,
    // except it's now operating on memrefs provided by cinm.
    // This can be lowered to affine or whatever afterwards.
    builder.setInsertionPointToStart(&launchBlock);
    auto innerReduce = builder.create<linalg::ReduceOp>(
        // no results bc memref
        TypeRange{}, reduceInpts, reduceInits,
        // todo we are hardcoding the dimensions
        // This is because we flatten everything. This does not support
        // custom reduction dimensions.
        ArrayRef<int64_t>{0});

    IRMapping irMapping;
    reduction.getRegion().cloneInto(&innerReduce.getRegion(), irMapping);
    builder.create<cnm::TerminatorOp>();
  }
  builder.setInsertionPointAfter(launchOp);

  // gather the results (only the out buffers)

  llvm::SmallVector<Value, 1> newResults;
  for (size_t i = 0; i < reduction->getNumResults(); i++) {
    auto result = reduction.getResult(i);
    auto alloc = launchOperands[reduction.getNumDpsInputs() + i];
    auto map = affineMaps[reduction.getNumDpsInputs() + i];
    auto res = builder.create<cnm::GatherOp>(
        result.getType(), cnm::GatherTokenType::get(builder.getContext()),
        alloc, workgroup, map);
    newResults.push_back(res.getOutput());
  }
  return newResults;
}

bool workgroupFitsParallelDims(Type ty, cnm::WorkgroupType wgTy,
                               ArrayRef<int64_t> reductionDims) {
  // reductionDims is sorted
  if (auto tensorTy = ty.dyn_cast<RankedTensorType>()) {
    auto shape = tensorTy.getShape();
    int64_t numNonReduceElts = 1;
    for (size_t i = 0, j = 0; i < shape.size(); i++) {
      if (j >= reductionDims.size() || i != reductionDims[j++]) {
        numNonReduceElts *= shape[i];
      }
    }
    return numNonReduceElts % wgTy.getNumElements() == 0;
  }
  return false;
}

struct WorkGroupMakerStrategy {
  static std::optional<cnm::WorkgroupType>
  determineWorkGroupTypeForRewrite(linalg::ReduceOp op);
};

// this strategy just tries to use a static workgroup shape
template <unsigned... Shape> struct StaticWorkGroup {
  static std::optional<cnm::WorkgroupType>
  determineWorkGroupTypeForRewrite(linalg::ReduceOp op) {
    cnm::WorkgroupType wgTy =
        cnm::WorkgroupType::get(op.getContext(), {Shape...});

    // output ops need to be big enough to be dispatchable on the WG
    if (llvm::any_of(op.getDpsInits(), [&](Value v) {
          return !workgroupFitsParallelDims(v.getType(), wgTy, {});
        }))
      return std::nullopt;

    // in particular the input operands WITHOUT the reduction dimensions should
    // fit
    if (llvm::any_of(op.getDpsInputs(), [&](Value v) {
          return !workgroupFitsParallelDims(v.getType(), wgTy,
                                            op.getDimensions());
        }))
      return std::nullopt;
    return wgTy;
  }
};

template <typename WGStrat>
struct ConvertLinalgReduceIntoLaunch
    : public OpConversionPattern<linalg::ReduceOp> {
  using OpConversionPattern::OpConversionPattern;

  LogicalResult
  matchAndRewrite(linalg::ReduceOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {

    ImplicitLocOpBuilder builder(op->getLoc(), rewriter);
    auto wgTyOpt = WGStrat::determineWorkGroupTypeForRewrite(op);
    if (!wgTyOpt)
      return failure();
    cnm::WorkgroupType wgTy = *wgTyOpt;
    Value workgroup = builder.create<cnm::WorkgroupOp>(wgTy);

    auto results =
        convertLinalgReduceIntoLaunch(builder, op, adaptor, workgroup, wgTy);
    rewriter.replaceOp(op, results);

    return success();
  }
};

struct ConvertTiledCinmToCnm
    : public ConvertTiledCinmToCnmBase<ConvertTiledCinmToCnm> {

  using WorkGroupMakerStrategy = StaticWorkGroup<4, 16>;

  void runOnOperation() override {
    RewritePatternSet patterns(&getContext());
    patterns.insert<ConvertLinalgReduceIntoLaunch<WorkGroupMakerStrategy>>(
        &getContext());
    ConversionTarget target(getContext());

    target.addDynamicallyLegalOp<linalg::ReduceOp>(
        [](linalg::ReduceOp op) -> bool {
          return !WorkGroupMakerStrategy::determineWorkGroupTypeForRewrite(op);
        });
    target.markUnknownOpDynamicallyLegal([](...) { return true; });

    if (failed(
            applyFullConversion(getOperation(), target, std::move(patterns))))
      signalPassFailure();
  }
};

} // namespace

std::unique_ptr<Pass> mlir::cinm::createConvertTiledCinmToCnmPass() {
  return std::make_unique<ConvertTiledCinmToCnm>();
}

void mlir::cinm::registerCinmToCnmPipeline() {
  // todo
}
