//===- qir2qasm.cpp - QIR → OpenQASM Translation ------------------------===//
//
// A small, maintainable translator that handles AllocOp and HOp.
// Uses one pass over the module, flat dispatch with TypeSwitch,
// and per-op printer methods.
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/Arith/IR/Arith.h" // for arith::ConstantOp
#include "mlir/IR/BuiltinOps.h"          // ModuleOp
#include "mlir/Support/LogicalResult.h"  // LogicalResult, success(), failure()
#include "mlir/Tools/mlir-translate/Translation.h"
#include "quantum-mlir/Dialect/QIR/IR/QIR.h"
#include "quantum-mlir/Dialect/QIR/IR/QIROps.h" // AllocOp, HOp

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/TypeSwitch.h" // TypeSwitch
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"

using namespace mlir;
using namespace qir;

namespace {
class QASMTranslation {
public:
    QASMTranslation(ModuleOp m, raw_ostream &o) : module(m), os(o) {}

    LogicalResult translate()
    {
        emitHeader();
        auto result = module.walk<WalkOrder::PreOrder>([&](Operation* op) {
            return TypeSwitch<Operation*, WalkResult>(op)
                .Case<AllocOp>([&](AllocOp a) { return handleQubitAlloc(a); })
                .Case<AllocResultOp>(
                    [&](AllocResultOp r) { return handleCRegAlloc(r); })
                .Case<HOp>([&](HOp h) { return handleH(h); })
                .Case<XOp>([&](XOp x) { return handleX(x); })
                .Case<CNOTOp>([&](CNOTOp c) { return handleCNOT(c); })
                .Case<U3Op>([&](U3Op u) { return handleU3(u); })
                .Case<MeasureOp>([&](MeasureOp m) { return handleMeasure(m); })
                .Case<ResetOp>([&](ResetOp r) { return handleReset(r); })
                .Default([&](Operation*) { return WalkResult::advance(); });
        });
        return result.wasInterrupted() ? failure() : success();
    }

private:
    ModuleOp module;
    raw_ostream &os;

    // Two counters + name maps
    unsigned nextQ = 0, nextC = 0;
    llvm::DenseMap<Value, std::string> qubitNames;
    llvm::DenseMap<Value, std::string> cregNames;

    void emitHeader()
    {
        os << "OPENQASM 2.0;\n"
              "include \"qelib1.inc\";\n\n";
    }

    // Qubit allocation → qreg qN[1];
    WalkResult handleQubitAlloc(AllocOp op)
    {
        std::string name = "q" + std::to_string(nextQ++);
        qubitNames[op.getResult()] = name;
        os << "qreg " << name << "[1];\n";
        return WalkResult::advance();
    }

    // Result‐register allocation → creg cN[1];
    WalkResult handleCRegAlloc(AllocResultOp op)
    {
        std::string name = "c" + std::to_string(nextC++);
        cregNames[op.getResult()] = name;
        os << "creg " << name << "[1];\n";
        return WalkResult::advance();
    }

    // H gate → h qN;
    WalkResult handleH(HOp op)
    {
        auto name = qubitNames.lookup(op.getOperand());
        os << "h " << name << ";\n";
        return WalkResult::advance();
    }

    // X gate → x qN;
    WalkResult handleX(XOp op)
    {
        auto name = qubitNames.lookup(op.getOperand());
        os << "x " << name << ";\n";
        return WalkResult::advance();
    }

    // CNOT → cx qN, qM;
    WalkResult handleCNOT(CNOTOp op)
    {
        auto c0 = qubitNames.lookup(op.getOperand(0));
        auto c1 = qubitNames.lookup(op.getOperand(1));
        os << "cx " << c0 << ", " << c1 << ";\n";
        return WalkResult::advance();
    }

    // U3 → U(theta,phi,lambda) qN;
    WalkResult handleU3(U3Op op)
    {
        auto getD = [&](Value v) {
            auto c = v.getDefiningOp<arith::ConstantOp>();
            assert(c && "U3 parameters must be arith.constant");
            return c.getValue().cast<FloatAttr>().getValueAsDouble();
        };
        double t = getD(op.getOperand(1));
        double p = getD(op.getOperand(2));
        double l = getD(op.getOperand(3));
        auto name = qubitNames.lookup(op.getOperand(0));
        os << "U(" << t << "," << p << "," << l << ") " << name << ";\n";
        return WalkResult::advance();
    }

    // Measure → measure qN -> cM[0];
    WalkResult handleMeasure(MeasureOp op)
    {
        auto qn = qubitNames.lookup(op.getOperand(0));
        auto cn = cregNames.lookup(op.getOperand(1));
        os << "measure " << qn << " -> " << cn << "[0];\n";
        return WalkResult::advance();
    }

    // Reset → reset qN;
    WalkResult handleReset(ResetOp op)
    {
        auto name = qubitNames.lookup(op.getOperand());
        os << "reset " << name << ";\n";
        return WalkResult::advance();
    }
};
} // end anonymous namespace

namespace mlir {
void registerToOpenQASMTranslation()
{
    static TranslateFromMLIRRegistration reg(
        "mlir-to-openqasm",
        "Translate QIR dialect to OpenQASM 2.0",
        [](ModuleOp m, raw_ostream &os) -> LogicalResult {
            return QASMTranslation(m, os).translate();
        },
        [](DialectRegistry &dr) {
            dr.insert<qir::QIRDialect, arith::ArithDialect>();
        });
}
} // namespace mlir
