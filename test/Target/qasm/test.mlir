// RUN: quantum-translate --mlir-to-openqasm %s | FileCheck %s

module {
  // Allocate three qubits

// CHECK-DAG: OPENQASM 2.0;
// CHECK-DAG: include "qelib1.inc";
// CHECK-DAG: qreg q0[1];
// CHECK-DAG: qreg q1[1];
// CHECK-DAG: qreg q2[1];
  %q0 = "qir.alloc"  () : () -> (!qir.qubit)
  %q1 = "qir.alloc"  () : () -> (!qir.qubit)
  %q2 = "qir.alloc"  () : () -> (!qir.qubit)

  // Some constant parameters for U3
  %c1 = arith.constant 0.1 : f64
  %c2 = arith.constant 0.2 : f64
  %c3 = arith.constant 0.3 : f64

  // Allocate one result register for measurement. 
  //CHECK-DAG: creg c1[1];
  //CHECK-DAG: creg c2[1];
  %r0 = "qir.ralloc" () : () -> (!qir.result)
  %r1 = "qir.ralloc" () : () -> (!qir.result)
  // A variety of gates and ops
  // CHECK-DAG: h q0;
  "qir.H"        (%q0)                  : (!qir.qubit) -> ()
  // CHECK-DAG: x q1;
  "qir.X"        (%q1)                  : (!qir.qubit) -> ()
  // CHECK-DAG: U({{.*}}) q2;
  "qir.U3"       (%q2, %c1, %c2, %c3)   : (!qir.qubit, f64, f64, f64) -> ()
  // CHECK-DAG: cx q0, q1;
  "qir.CNOT"     (%q0, %q1)             : (!qir.qubit, !qir.qubit)     -> ()
  // CHECK-DAG: measure q2 -> c1[0];
  "qir.measure"  (%q2, %r1)             : (!qir.qubit, !qir.result)    -> ()
  // CHECK-DAG: reset q1;
  "qir.reset"    (%q1)                  : (!qir.qubit)                 -> ()
}
