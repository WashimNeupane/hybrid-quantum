/// Implements the ConvertQuantumToQIRPass.
///
/// @file
/// @author     Lars Schütze (lars.schuetze@tu-dresden.de)

#include "cinm-mlir/Conversion/QuantumToQIR/QuantumToQIR.h"

#include "cinm-mlir/Dialect/Quantum/IR/Quantum.h"
#include "cinm-mlir/Dialect/QIR/IR/QIR.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

using namespace mlir;
using namespace mlir::quantum;

//===- Generated includes -------------------------------------------------===//

namespace mlir {

#define GEN_PASS_DEF_CONVERTQUANTUMTOQIR
#include "cinm-mlir/Conversion/Passes.h.inc"

} // namespace mlir

//===----------------------------------------------------------------------===//

namespace {

struct ConvertQuantumToQIRPass
        : mlir::impl::ConvertQuantumToQIRBase<ConvertQuantumToQIRPass> {
    using ConvertQuantumToQIRBase::ConvertQuantumToQIRBase;

    void runOnOperation() override;
};

struct QuantumToQirQubitTypeMapping {
public:
    QuantumToQirQubitTypeMapping() : map() {}

    // Map a `quantum.Qubit` to (possible many) `qir.qubit`
    void allocate(Value quantum, ValueRange qir) {
        if(!map.contains(quantum)) {
            llvm::SmallVector<Value> elem;
            map.insert({quantum, elem});
        }
        for (auto qubit : qir) {
            map[quantum].push_back(qubit);
        }
    }

private:
    llvm::DenseMap<Value, llvm::SmallVector<Value>> map;
};

template <typename Op>
struct QuantumToQIROpConversion : OpConversionPattern<Op> {
    QuantumToQIROpConversion(MLIRContext *context, QuantumToQirQubitTypeMapping *qubitMap)
        : OpConversionPattern<Op>(context, /* benefit */ 1), qubitMap(qubitMap) {}

    QuantumToQirQubitTypeMapping *qubitMap;
};

struct ConvertAlloc : public QuantumToQIROpConversion<quantum::AllocOp> {
    using QuantumToQIROpConversion::QuantumToQIROpConversion;

    LogicalResult matchAndRewrite(
        AllocOp op,
        AllocOpAdaptor adaptor,
        ConversionPatternRewriter &rewriter) const override
    {
        //MLIRContext *ctx = getContext();
        //const TypeConverter *conv = getTypeConverter();
        unsigned size = op.getType().getSize();
        llvm::SmallVector<Value> qubits;
        for(unsigned i = 0; i < size; i++) {
            auto qubit = rewriter.create<qir::AllocOp>(
                op.getLoc(),
                qir::QubitType::get(getContext()));
            qubits.push_back(qubit);
        }
        qubitMap->allocate(op.getResult(), qubits);
        rewriter.eraseOp(op);
        return success();
    }
}; // struct ConvertAllocOp

} // namespace

void ConvertQuantumToQIRPass::runOnOperation()
{
  TypeConverter typeConverter;
  ConversionTarget target(getContext());
  RewritePatternSet patterns(&getContext());

  quantum::populateConvertQuantumToQIRPatterns(typeConverter, patterns);

  target.addIllegalDialect<quantum::QuantumDialect>();
  target.addLegalDialect<qir::QIRDialect>();

  if (failed(applyPartialConversion(
          getOperation(),
          target,
          std::move(patterns)))) {
    return signalPassFailure();
  }
}

void mlir::quantum::populateConvertQuantumToQIRPatterns(
    TypeConverter &typeConverter,
    RewritePatternSet &patterns)
{
    QuantumToQirQubitTypeMapping qubitMap;

    patterns.add<
        ConvertAlloc>(patterns.getContext(), &qubitMap);
}

std::unique_ptr<Pass> mlir::createConvertQuantumToQIRPass() {
    return std::make_unique<ConvertQuantumToQIRPass>();
}


