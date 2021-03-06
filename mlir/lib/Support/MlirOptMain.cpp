//===- MlirOptMain.cpp - MLIR Optimizer Driver ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a utility that runs an optimization pass and prints the result back
// out. It is designed to support unit testing.
//
//===----------------------------------------------------------------------===//

#include "mlir/Support/MlirOptMain.h"
#include "mlir/IR/AsmState.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Module.h"
#include "mlir/Parser.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Support/FileUtilities.h"
#include "mlir/Support/ToolUtilities.h"
#include "mlir/Transforms/Passes.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/ToolOutputFile.h"

using namespace mlir;
using namespace llvm;
using llvm::SMLoc;

/// Perform the actions on the input file indicated by the command line flags
/// within the specified context.
///
/// This typically parses the main source file, runs zero or more optimization
/// passes, then prints the output.
///
static LogicalResult performActions(raw_ostream &os, bool verifyDiagnostics,
                                    bool verifyPasses, SourceMgr &sourceMgr,
                                    MLIRContext *context,
                                    const PassPipelineCLParser &passPipeline) {
  // Disable multi-threading when parsing the input file. This removes the
  // unnecessary/costly context synchronization when parsing.
  bool wasThreadingEnabled = context->isMultithreadingEnabled();
  context->disableMultithreading();

  // Parse the input file and reset the context threading state.
  OwningModuleRef module(parseSourceFile(sourceMgr, context));
  context->enableMultithreading(wasThreadingEnabled);
  if (!module)
    return failure();

  // Apply any pass manager command line options.
  PassManager pm(context, verifyPasses);
  applyPassManagerCLOptions(pm);

  // Build the provided pipeline.
  if (failed(passPipeline.addToPipeline(pm)))
    return failure();

  // Run the pipeline.
  if (failed(pm.run(*module)))
    return failure();

  // Print the output.
  module->print(os);
  os << '\n';
  return success();
}

/// Parses the memory buffer.  If successfully, run a series of passes against
/// it and print the result.
static LogicalResult processBuffer(raw_ostream &os,
                                   std::unique_ptr<MemoryBuffer> ownedBuffer,
                                   bool verifyDiagnostics, bool verifyPasses,
                                   bool allowUnregisteredDialects,
                                   const PassPipelineCLParser &passPipeline) {
  // Tell sourceMgr about this buffer, which is what the parser will pick up.
  SourceMgr sourceMgr;
  sourceMgr.AddNewSourceBuffer(std::move(ownedBuffer), SMLoc());

  // Parse the input file.
  MLIRContext context;
  context.allowUnregisteredDialects(allowUnregisteredDialects);
  context.printOpOnDiagnostic(!verifyDiagnostics);

  // If we are in verify diagnostics mode then we have a lot of work to do,
  // otherwise just perform the actions without worrying about it.
  if (!verifyDiagnostics) {
    SourceMgrDiagnosticHandler sourceMgrHandler(sourceMgr, &context);
    return performActions(os, verifyDiagnostics, verifyPasses, sourceMgr,
                          &context, passPipeline);
  }

  SourceMgrDiagnosticVerifierHandler sourceMgrHandler(sourceMgr, &context);

  // Do any processing requested by command line flags.  We don't care whether
  // these actions succeed or fail, we only care what diagnostics they produce
  // and whether they match our expectations.
  performActions(os, verifyDiagnostics, verifyPasses, sourceMgr, &context,
                 passPipeline);

  // Verify the diagnostic handler to make sure that each of the diagnostics
  // matched.
  return sourceMgrHandler.verify();
}

LogicalResult mlir::MlirOptMain(raw_ostream &outputStream,
                                std::unique_ptr<MemoryBuffer> buffer,
                                const PassPipelineCLParser &passPipeline,
                                bool splitInputFile, bool verifyDiagnostics,
                                bool verifyPasses,
                                bool allowUnregisteredDialects) {
  // The split-input-file mode is a very specific mode that slices the file
  // up into small pieces and checks each independently.
  if (splitInputFile)
    return splitAndProcessBuffer(
        std::move(buffer),
        [&](std::unique_ptr<MemoryBuffer> chunkBuffer, raw_ostream &os) {
          return processBuffer(os, std::move(chunkBuffer), verifyDiagnostics,
                               verifyPasses, allowUnregisteredDialects,
                               passPipeline);
        },
        outputStream);

  return processBuffer(outputStream, std::move(buffer), verifyDiagnostics,
                       verifyPasses, allowUnregisteredDialects, passPipeline);
}

LogicalResult mlir::MlirOptMain(int argc, char **argv, StringRef toolName) {
  static cl::opt<std::string> inputFilename(
      cl::Positional, cl::desc("<input file>"), cl::init("-"));

  static cl::opt<std::string> outputFilename("o", cl::desc("Output filename"),
                                             cl::value_desc("filename"),
                                             cl::init("-"));

  static cl::opt<bool> splitInputFile(
      "split-input-file",
      cl::desc("Split the input file into pieces and process each "
               "chunk independently"),
      cl::init(false));

  static cl::opt<bool> verifyDiagnostics(
      "verify-diagnostics",
      cl::desc("Check that emitted diagnostics match "
               "expected-* lines on the corresponding line"),
      cl::init(false));

  static cl::opt<bool> verifyPasses(
      "verify-each",
      cl::desc("Run the verifier after each transformation pass"),
      cl::init(true));

  static cl::opt<bool> allowUnregisteredDialects(
      "allow-unregistered-dialect",
      cl::desc("Allow operation with no registered dialects"), cl::init(false));

  static cl::opt<bool> showDialects(
      "show-dialects", cl::desc("Print the list of registered dialects"),
      cl::init(false));

  InitLLVM y(argc, argv);

  // Register any command line options.
  registerAsmPrinterCLOptions();
  registerMLIRContextCLOptions();
  registerPassManagerCLOptions();
  PassPipelineCLParser passPipeline("", "Compiler passes to run");

  // Build the list of dialects as a header for the --help message.
  std::string helpHeader = (toolName + "\nAvailable Dialects: ").str();
  {
    llvm::raw_string_ostream os(helpHeader);
    MLIRContext context;
    interleaveComma(context.getRegisteredDialects(), os, [&](Dialect *dialect) {
      StringRef name = dialect->getNamespace();
      // filter the builtin dialect.
      if (name.empty())
        os << "<builtin>";
      else
        os << name;
    });
  }
  // Parse pass names in main to ensure static initialization completed.
  cl::ParseCommandLineOptions(argc, argv, helpHeader);

  if (showDialects) {
    llvm::outs() << "Registered Dialects:\n";
    MLIRContext context;
    interleave(
        context.getRegisteredDialects(), llvm::outs(),
        [](Dialect *dialect) { llvm::outs() << dialect->getNamespace(); },
        "\n");
    return success();
  }

  // Set up the input file.
  std::string errorMessage;
  auto file = openInputFile(inputFilename, &errorMessage);
  if (!file) {
    llvm::errs() << errorMessage << "\n";
    return failure();
  }

  auto output = openOutputFile(outputFilename, &errorMessage);
  if (!output) {
    llvm::errs() << errorMessage << "\n";
    return failure();
  }

  if (failed(MlirOptMain(output->os(), std::move(file), passPipeline,
                         splitInputFile, verifyDiagnostics, verifyPasses,
                         allowUnregisteredDialects)))
    return failure();

  // Keep the output file if the invocation of MlirOptMain was successful.
  output->keep();
  return success();
}
