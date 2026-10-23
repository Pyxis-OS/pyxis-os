# Compiling with Clang

Use a networked space where you can install packages, such as Development in a
live boot. Configure your repositories first if needed; see
[Packages with pkg](packages.md).

## Compile and run C

1. Install the compiler and pkg-config:

   ```text
   pkg install clang-hosted pkgconf
   ```

   At `Switch to this package set? [y/N]`, type `y`.

   `clang-hosted` also installs `pyxis-dev` and `libcxx-dev`. Their headers,
   startup files and static libraries appear in `sdk://`. Clang searches
   `sdk://include` and `sdk://lib` automatically; no sysroot setup is needed.

2. Make a working directory and open a source file:

   ```text
   mkdir home://hello
   cd home://hello
   vi hello.c
   ```

   Press `i`, type this, then press Escape and type `:wq` followed by Enter:

   ```c
   #include <stdio.h>

   int main(void)
   {
       puts("Hello, Pyxis!");
       return 0;
   }
   ```

3. Compile with no flags, then run the default output:

   ```text
   clang hello.c
   ./a.pxe
   Hello, Pyxis!
   ```

   A successful compile prints nothing. Clang invokes its linker itself.
   To choose a name, use `clang hello.c -o hello.pxe`, then `./hello.pxe`.
   `./` selects a file in your working directory.
   If compilation reports an error, fix it before running: an older output
   file can still exist.

## Compile C++

Create `hello.cpp` with `vi` the same way:

```cpp
#include <iostream>

int main()
{
    std::cout << "Hello, C++!\n";
}
```

Use `clang++` to select the C++ headers and runtime:

```text
clang++ hello.cpp -o hello-cpp.pxe
./hello-cpp.pxe
Hello, C++!
```

## Use a library

1. Install its development package:

   ```text
   pkg install libpng-dev
   ```

   Confirm the new package set with `y`. Dependencies such as zlib are
   installed automatically. Their headers and libraries join `sdk://`;
   no per-package include or library paths are needed. Check its flags:

   ```text
   pkg-config --cflags --libs libpng
   -lpng -lz
   ```

2. Create `png-version.c`:

   ```c
   #include <png.h>
   #include <stdio.h>

   int main(void)
   {
       printf("libpng %s\n", png_get_libpng_ver(NULL));
       return 0;
   }
   ```

3. Compile and run it:

   ```text
   pkg-config --cflags --libs libpng > png-flags.rsp
   clang png-version.c -o png-version.pxe @png-flags.rsp
   ./png-version.pxe
   libpng 1.6.59
   ```

   `@png-flags.rsp` reads pkg-config's flags from the saved file. Keep library
   flags after the source files. Libraries are linked statically; the resulting
   program does not need the `-dev` packages to run.

## Use clangd in Neovim

`clang-hosted` includes clangd. Open your source file:

```text
nvim hello.c
```

Enable it for that session:

```vim
:lua vim.lsp.enable('clangd')
```

Clangd attaches to C/C++ buffers and underlines errors. For automatic startup,
put `vim.lsp.enable('clangd')` in `home://.config/nvim/init.lua`; the
[Neovim setup guide](../userland/neovim.md#language-servers) includes a ready-made
example with completion enabled. Restart Neovim after installing more `-dev`
packages so clangd sees the new SDK view.
