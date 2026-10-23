// Emit an x86-64 object into owned memory; do not execute generated code.
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/ExecutionEngine/Orc/Shared/SimpleRemoteEPCUtils.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/LibraryScanner.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

class UnusedClient : public llvm::orc::SimpleRemoteEPCTransportClient {
public:
  bool Called = false;
  llvm::Expected<HandleMessageAction>
  handleMessage(llvm::orc::SimpleRemoteEPCOpcode, uint64_t,
                llvm::orc::ExecutorAddr,
                llvm::orc::shared::WrapperFunctionBuffer) override {
    Called = true;
    return EndSession;
  }
  void handleDisconnect(llvm::Error Error) override {
    Called = true;
    llvm::consumeError(std::move(Error));
  }
};

static bool refusedUnsupportedPaths() {
  UnusedClient client;
  auto transport = llvm::orc::FDSimpleRemoteEPCTransport::Create(client, 0, 1);
  if (transport)
    std::_Exit(1);
  if (llvm::errorToErrorCode(transport.takeError()) !=
      std::make_error_code(std::errc::operation_not_supported) || client.Called)
    return false;
  auto cache = std::make_shared<llvm::orc::LibraryPathCache>();
  llvm::orc::PathResolver paths(cache);
  std::error_code error;
  auto path = paths.resolve("tmp://unsupported-shared-library", error);
  if (path || error != std::make_error_code(std::errc::operation_not_supported))
    return false;
  llvm::orc::DylibPathValidator validator(paths, *cache);
  return !validator.isSharedLibrary("tmp://unsupported-shared-library");
}

int main(int argc, char **argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: llvm-object-check OUTPUT\n");
    return 1;
  }
  if (!refusedUnsupportedPaths())
    return 1;
  std::puts("LLVM_ORC_REFUSAL COMPLETE transport+shared-library=NOT_SUPPORTED");
  if (llvm::InitializeNativeTarget() || llvm::InitializeNativeTargetAsmPrinter())
    return 1;
  llvm::LLVMContext context;
  llvm::Module module("pyxis-development", context);
  const llvm::Triple triple("x86_64-unknown-pyxis");
  module.setTargetTriple(triple);
  std::string error;
  const auto *target = llvm::TargetRegistry::lookupTarget(triple.str(), error);
  if (!target) {
    llvm::errs() << error << '\n';
    return 1;
  }
  llvm::TargetOptions options;
  std::unique_ptr<llvm::TargetMachine> machine(target->createTargetMachine(
      triple, "x86-64", "", options, llvm::Reloc::PIC_));
  if (!machine)
    return 1;
  module.setDataLayout(machine->createDataLayout());
  auto *type = llvm::FunctionType::get(llvm::Type::getInt32Ty(context), false);
  auto *function = llvm::Function::Create(type, llvm::GlobalValue::ExternalLinkage,
                                         "pyxis_answer", module);
  llvm::IRBuilder<> builder(llvm::BasicBlock::Create(context, "entry", function));
  builder.CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 42));
  llvm::SmallVector<char, 0> object;
  llvm::raw_svector_ostream stream(object);
  llvm::legacy::PassManager passes;
  if (machine->addPassesToEmitFile(passes, stream, nullptr,
                                  llvm::CodeGenFileType::ObjectFile))
    return 1;
  passes.run(module);
  if (object.size() < 64 || object[0] != '\x7f' || object[1] != 'E' ||
      object[2] != 'L' || object[3] != 'F' || object[4] != 2 || object[5] != 1 ||
      static_cast<unsigned char>(object[18]) != 62 || object[19] != 0)
    return 1;
  FILE *output = std::fopen(argv[1], "wb");
  if (!output)
    return 1;
  const bool written = std::fwrite(object.data(), 1, object.size(), output) == object.size();
  const int closed = std::fclose(output);
  if (!written || closed)
    return 1;
  std::printf("LLVM_OBJECT COMPLETE target=x86_64 bytes=%zu execution=NONE\n", object.size());
  return 0;
}
