# Part of the opt-in target LLVM development build, not the compiler container.
# CMake's legacy engine alias adds the removed LLVMjit archive. Name the
# concrete libraries used by the config-tool's engine group instead.
set(LLVM_LINK_COMPONENTS
  BitWriter MCDisassembler Core ExecutionEngine ScalarOpts
  TransformUtils InstCombine native MCJIT Interpreter X86TargetMCA OrcJIT)
add_llvm_executable(pyxis-llvm-object-check object-check.cpp)
