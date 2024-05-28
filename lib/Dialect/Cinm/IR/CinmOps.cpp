/// Implements the Cinm dialect ops.
///
/// @file

#include "cinm-mlir/Dialect/Cinm/IR/CinmOps.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/TypeUtilities.h"

#include "llvm/ADT/APFloat.h"

#define DEBUG_TYPE "cinm-ops"

using namespace mlir;
using namespace mlir::cinm;

//===- Generated implementation -------------------------------------------===//

#include "cinm-mlir/Dialect/Cinm/IR/CinmEnums.cpp.inc"

#define GET_OP_CLASSES
#include "cinm-mlir/Dialect/Cinm/IR/CinmOps.cpp.inc"

//===----------------------------------------------------------------------===//
// CinmDialect
//===----------------------------------------------------------------------===//

void CinmDialect::registerOps() {
  addOperations<
#define GET_OP_LIST
#include "cinm-mlir/Dialect/Cinm/IR/CinmOps.cpp.inc"
      >();
}

namespace mlir {
namespace cinm {

::mlir::ParseResult MinOp::parse(::mlir::OpAsmParser &parser,
                                 ::mlir::OperationState &result) {
  return MaxOp::parse(parser, result);
}

::mlir::ParseResult MaxOp::parse(::mlir::OpAsmParser &parser,
                                 ::mlir::OperationState &result) {
  OpAsmParser::UnresolvedOperand input;
  Type inputType;
  if (parser.parseOperand(input) || parser.parseColonType(inputType))
    return failure();

  SmallVector<Value> typedInput;
  if (parser.resolveOperand(input, inputType, typedInput))
    return failure();

  result.addOperands(typedInput);

  if (auto shaped = dyn_cast<ShapedType>(inputType)) {
    result.addTypes({shaped.getElementType()});
    return success();
  } else {
    return parser.emitError(parser.getCurrentLocation(),
                            "Expected a tensor type as operand");
  }
}

void printMinMaxOp(Value v, ::mlir::OpAsmPrinter &p) {
  p.printOperand(v);
  p << ": ";
  p.printType(v.getType());
}
void MaxOp::print(::mlir::OpAsmPrinter &p) { printMinMaxOp(getOperand(), p); }

void MinOp::print(::mlir::OpAsmPrinter &p) { printMinMaxOp(getOperand(), p); }

::mlir::LogicalResult SimSearchOp::inferReturnTypeComponents(
    ::mlir::MLIRContext *context, std::optional<::mlir::Location> location,
    Adaptor adaptor,
    ::llvm::SmallVectorImpl<::mlir::ShapedTypeComponents>
        &inferredReturnShapes) {

  ShapeAdaptor inputShape(adaptor.getLeft().getType());
  auto elt = inputShape.getElementType();

  SmallVector<int64_t> outputShape;
  outputShape.resize(1, ShapedType::kDynamic);
  inferredReturnShapes.push_back(ShapedTypeComponents(outputShape, elt));
  inferredReturnShapes.push_back(ShapedTypeComponents(outputShape, elt));
  return success();
}

::mlir::LogicalResult TopKOp::inferReturnTypeComponents(
    ::mlir::MLIRContext *context, std::optional<::mlir::Location> location,
    Adaptor adaptor,
    ::llvm::SmallVectorImpl<::mlir::ShapedTypeComponents>
        &inferredReturnShapes) {

  ShapeAdaptor inputShape(adaptor.getInput().getType());
  auto elt = inputShape.getElementType();

  SmallVector<int64_t> outputShape;
  outputShape.resize(1, ShapedType::kDynamic);
  inferredReturnShapes.push_back(ShapedTypeComponents(outputShape, elt));
  inferredReturnShapes.push_back(ShapedTypeComponents(outputShape, elt));
  return success();
}

// Copied from the TOSA codebase.
::mlir::LogicalResult TransposeOp::inferReturnTypeComponents(
    ::mlir::MLIRContext *context, std::optional<::mlir::Location> location,
    Adaptor adaptor,
    ::llvm::SmallVectorImpl<::mlir::ShapedTypeComponents>
        &inferredReturnShapes) {
  ShapeAdaptor inputShape(adaptor.getInput1().getType());
  ShapeAdaptor permsShape(adaptor.getPerms().getType());

  // If input rank and permutation length is unknown, the output rank is
  // unknown.
  if (!inputShape.hasRank() || !permsShape.hasRank() ||
      permsShape.isDynamicDim(0)) {
    inferredReturnShapes.push_back(ShapedTypeComponents());
    return success();
  }

  // This would imply the number of permutations does not match the rank of the
  // input which is illegal.
  if (permsShape.getDimSize(0) != inputShape.getRank()) {
    return failure();
  }

  // Without the input dims we cannot determine the output dim sizes but we
  // can determine the output rank.
  SmallVector<int64_t> outputShape;
  if (!inputShape.hasRank()) {
    outputShape.resize(permsShape.getDimSize(0), ShapedType::kDynamic);
    inferredReturnShapes.push_back(ShapedTypeComponents(outputShape));
    return success();
  }

  // Rank-0 means no permutations matter.
  if (inputShape.getRank() == 0) {
    inferredReturnShapes.push_back(ShapedTypeComponents(outputShape));
    return success();
  }

  // Check whether the input dimensions are all the same.
  bool allTheSame = true;
  for (int i = 1, s = inputShape.getRank(); i < s; i++) {
    if (inputShape.getDimSize(0) != inputShape.getDimSize(i)) {
      allTheSame = false;
      break;
    }
  }

  // If all of the input dimensions are the same we don't care about the
  // permutation.
  if (allTheSame) {
    outputShape.resize(inputShape.getRank(), inputShape.getDimSize(0));
    inferredReturnShapes.push_back(ShapedTypeComponents(outputShape));
    return success();
  }

  outputShape.resize(inputShape.getRank(), ShapedType::kDynamic);
  // If the permuations are a constant we can directly determine the output
  // shape.
  DenseIntElementsAttr attr;
  if (matchPattern(adaptor.getPerms(), m_Constant(&attr)) &&
      attr.getType().getRank() == 1) {
    ShapeAdaptor permShape = attr;
    outputShape.reserve(inputShape.getRank());
    for (int i = 0, s = inputShape.getRank(); i < s; i++) {
      outputShape[i] = inputShape.getDimSize(permShape.getDimSize(i));
    }
  }

  inferredReturnShapes.push_back(ShapedTypeComponents(outputShape));
  return success();
}

ParseResult
parseCaptureArgs(OpAsmParser &parser,
                 SmallVectorImpl<OpAsmParser::Argument> &lhs,
                 SmallVectorImpl<OpAsmParser::UnresolvedOperand> &rhs,
                 SmallVectorImpl<Type> &types) {
  auto parseElt = [&]() -> ParseResult {
    if (parser.parseArgument(lhs.emplace_back()) || parser.parseEqual() ||
        parser.parseOperand(rhs.emplace_back()) ||
        parser.parseColonType<Type>(types.emplace_back()))
      return failure();
    return success();
  };
  return parser.parseCommaSeparatedList(AsmParser::Delimiter::Paren, parseElt);
}

/*

    %rbuf = cinm.compute(%arg0 = %inpt : tensor<...>) -> tensor<...> {
      %flt = arith.constant <"...">: tensor<...>
      %conv = cinm.op.gemm %arg0, %flt: tensor<...>, tensor<...>
      cinm.yield %conv : tensor<...>
    }
*/
ParseResult parseComputeOp(OpAsmParser &parser, OperationState &result) {

  SmallVector<DictionaryAttr> resultAttrs;
  SmallVector<Type> resultTypes;

  SmallVector<OpAsmParser::Argument, 4> capturedParams;
  SmallVector<OpAsmParser::UnresolvedOperand, 4> capturedArgs;
  SmallVector<Type, 4> capturedArgsTypes;

  if (parseCaptureArgs(parser, capturedParams, capturedArgs, capturedArgsTypes))
    return failure();

  SmallVector<Value, 4> resolvedCaptArgs;
  if (parser.resolveOperands(std::move(capturedArgs), capturedArgsTypes,
                             parser.getCurrentLocation(), resolvedCaptArgs))
    return failure();

  result.addOperands(std::move(resolvedCaptArgs));

  SmallVector<Type, 4> resultTys;
  if (parser.parseArrowTypeList(resultTys))
    return parser.emitError(parser.getCurrentLocation(),
                            "Expected -> return type");
  result.addTypes(resultTys);

  std::string errorMessage;
  SmallVector<OpAsmParser::Argument> regionParams;
  regionParams.reserve(capturedParams.size());
  for (auto [arg, argT] : llvm::zip(capturedParams, capturedArgsTypes)) {
    arg.type = argT;
    regionParams.push_back(arg);
  }

  // Parse the body.
  auto *body = result.addRegion();
  SMLoc loc = parser.getCurrentLocation();
  OptionalParseResult parseResult =
      parser.parseRegion(*body, regionParams,
                         /*enableNameShadowing=*/false);
  if (parseResult.has_value()) {
    if (failed(*parseResult))
      return failure();
    // Function body was parsed, make sure its not empty.
    if (body->empty())
      return parser.emitError(loc, "expected non-empty cinm.compute body");
  }
  return success();
}

ParseResult ComputeOp::parse(OpAsmParser &parser, OperationState &result) {
  return parseComputeOp(parser, result);
}

} // namespace cinm
} // namespace mlir

// parsers/printers
