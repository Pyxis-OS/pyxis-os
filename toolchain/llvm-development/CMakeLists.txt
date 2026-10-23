# Part of the opt-in target LLVM development build, not the compiler container.
set(LLVM_LINK_COMPONENTS
  BitWriter engine MCDisassembler Core ExecutionEngine ScalarOpts
  TransformUtils InstCombine native MCJIT X86TargetMCA)
add_llvm_executable(pyxis-llvm-object-check object-check.cpp)
