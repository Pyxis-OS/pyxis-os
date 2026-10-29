# TCC, the first compiler on Pyxis

TCC was retired on 2026-10-22 (chronium). Programs are compiled inside Pyxis
with [hosted Clang](../development/hosted-tools.md), the `clang-hosted`
package; see the [edit/build/run walkthrough](../development/edit-build-run.md).

## History

- **2026-09-23:** the TCC port gave Pyxis its first compiler. `tcc hello.c -o
  hello.pxe` in the shell, then `./hello.pxe`, built and ran a program with
  nothing from the host. TCC wrote P1F executables directly and read a guest
  SDK at what became `boot://sdk`.
- **Later September and October:** TCC compiled Pyxis programs in place:
  - from host exports and on USB volumes;
  - in multiplexer panes and remote shells;
  - fetched over HTTPS;
  - with `#include` by URI.
  It built the first JVM on vela and the ThinkPad (see the
  [gallery](../gallery/README.md)).
- **From 2026-10-13:** hosted Clang and LLD ran inside Pyxis, with C++ and the
  full SDK. They reached the shell as the `clang-hosted` package.
- **2026-10-22:** TCC left `base` and the image build; hosted Clang is the
  compiler inside Pyxis. The planned [`sdk://` view](../wip/sdk-view.md) is
  what makes it the default, flag-free development compiler.

The port stays frozen in the ports repository's history: `tcc/` as of commit
`a8b6265`, with its recipe, patches and notes.
