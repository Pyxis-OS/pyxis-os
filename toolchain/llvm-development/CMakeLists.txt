# Target LLVM closure built into the compiler container and by the manual producer.
# CMake's legacy engine alias adds the removed LLVMjit archive. Name the
# concrete libraries used by the config-tool's engine group instead.
set(LLVM_LINK_COMPONENTS
  BitWriter MCDisassembler Core ExecutionEngine ScalarOpts
  TransformUtils InstCombine native MCJIT Interpreter X86TargetMCA OrcJIT)
add_llvm_executable(pyxis-llvm-object-check object-check.cpp)
