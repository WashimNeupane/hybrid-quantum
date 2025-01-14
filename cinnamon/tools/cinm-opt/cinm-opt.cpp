/// Main entry point for the cinm-mlir optimizer driver.
///
/// @file
/// @author      Karl F. A. Friebel (karl.friebel@tu-dresden.de)
/// @author      Clément Fournier (clement.fournier@tu-dresden.de)


#include "cinm-mlir/Dialect/Cinm/IR/CinmDialect.h"
#include "cinm-mlir/Dialect/Cinm/Transforms/Passes.h"
#include "cinm-mlir/Conversion/CinmPasses.h"

#include "cinm-mlir/Dialect/Cnm/IR/CnmDialect.h"
#include "cinm-mlir/Dialect/Cnm/Transforms/Passes.h"
#include "cinm-mlir/Conversion/CnmPasses.h"

#include "cinm-mlir/Dialect/UPMEM/IR/UPMEMDialect.h"
#include "cinm-mlir/Dialect/UPMEM/Transforms/Passes.h"
#include "cinm-mlir/Conversion/UPMEMPasses.h"
#include "cinm-mlir/Conversion/UPMEMToLLVM/UPMEMToLLVM.h"

#include "cinm-mlir/Dialect/Quantum/IR/QuantumDialect.h"
#include "cinm-mlir/Dialect/Quantum/Transforms/Passes.h"
#include "cinm-mlir/Conversion/QuantumPasses.h"

#include <mlir/IR/DialectRegistry.h>
#include <mlir/InitAllExtensions.h>

#include "mlir/IR/MLIRContext.h"
#include "mlir/InitAllDialects.h"
#include "mlir/InitAllPasses.h"
#include "mlir/Support/FileUtilities.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"

using namespace mlir;


int main(int argc, char *argv[]) {
  DialectRegistry registry;
  registerAllDialects(registry);

  registry.insert<cinm::CinmDialect, cnm::CnmDialect, upmem::UPMEMDialect, quantum::QuantumDialect>();

  registerAllPasses();
  registerAllExtensions(registry);
  registerCinmConversionPasses();
  registerCnmConversionPasses();
  registerQuantumConversionPasses();

  cnm::registerCnmBufferizationExternalModels(registry);
  cnm::registerCnmTransformsPasses();
  cinm::registerCinmTransformsPasses();
  upmem::registerConvertUpmemToLLvmInterface(registry);
  quantum::registerQuantumTransformsPasses();
 
  registerUPMEMTransformsPasses();
  registerUPMEMConversionPasses();


  return asMainReturnCode(
      MlirOptMain(argc, argv, "cinm-mlir optimizer driver\n", registry));
}
