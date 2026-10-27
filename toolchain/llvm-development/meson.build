project('pyxis-llvm-discovery', 'cpp')
llvm = dependency('llvm', method: 'config-tool', static: true,
  modules: ['bitwriter', 'engine', 'mcdisassembler', 'core', 'executionengine',
            'scalaropts', 'transformutils', 'instcombine', 'native', 'mcjit', 'orcjit'],
  optional_modules: ['coroutines', 'lto'])
assert(llvm.version().version_compare('>=23.1.3'), 'unexpected LLVM version')
message('LLVM_MESON_DISCOVERY COMPLETE version=' + llvm.version())
