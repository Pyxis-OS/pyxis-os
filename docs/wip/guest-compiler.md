# Choosing the first guest C compiler

Status: background comparison. The [TCC porting plan](tcc-port.md) is the current
route toward the [edit/build/run loop](edit-build-run.md). Keep these alternatives
available if that port exceeds its agreed boundaries; this is not a plan to
implement several compilers. FP/Mandelbrot is already complete.

## Candidates inspected

| Candidate | Existing output path | Implication for this milestone |
| --- | --- | --- |
| [TCC](https://github.com/TinyCC/tinycc/tree/3dc99dbc82f8e07308c5d398136803e62f9676df) | Integrated preprocessor, compiler, assembler and linker; ELF objects/archives and executables | Fewest separate guest tool dependencies among these candidates. Native I/O, libc, headers and executable layout still need work. |
| [chibicc](https://github.com/rui314/chibicc/tree/90d1f7f199cc55b13c7fdb5839d1409806633fdb) | C frontend emits assembly; driver invokes external `as` and `ld` | Readable code suitable for local adaptation, but needs an assembler/linker port or substantial new backend work to finish the loop. |
| [lacc](https://github.com/larmel/lacc/tree/30839843daaff9d87574b5854854c9ee4610cdcd) | Preprocessor/compiler can emit ELF object files directly; executable driver invokes system linker | Avoids a separate assembler for C output. Still needs a guest linker and a closer language/header audit; its advertised baseline is C89 plus later features. |
| [cproc](https://github.com/michaelforney/cproc/tree/d1c53ddf56571573a7025324c8dd5c6d547a4d1f) | C11/many C23 features, QBE backend; external assembler/linker and currently preprocessor | Worth considering for language support and a modular toolchain, but requires several guest tools for this first milestone. |

Pins identify the inspected snapshots, not approved dependency additions. No
candidate has been built for or run in Pyxis. TCC has host compile/link evidence;
chibicc has a host build and a header compilation attempt. Lacc/cproc were only
inspected for their documented pipeline and driver dependencies.

## What chibicc changes

Chibicc makes compiler internals easier to inspect and modify. Its `main.c`
already separates preprocessing/parsing/code generation, but compilation ends
in assembly text. Native launch calls could replace its fork/exec driver; that
alone would not supply the assembler and linker programs it invokes.

It still has runtime requirements absent from Pyxis: `strtold` in tokenization,
floating constant evaluation, memory streams, filesystem metadata and time
handling, plus Unix path/driver assumptions. Some can be replaced locally;
others remain useful libc work regardless of the compiler chosen.

A host build of pinned chibicc succeeded. Compiling the existing cat source to
assembly using our SDK and chibicc's headers stopped at the GNU `format`
attribute on a stdio declaration. Its unmodified driver also supplies Linux
include paths. This was a diagnostic attempt, not a target-header compatibility
pass. It does not establish that chibicc's later compiler stages fail, nor that
the required header changes would be large.

Chibicc deliberately retains allocations until exit and favors straightforward
code over memory economy. That is a tradeoff to measure with actual guest input
sizes, not a reason to reject it or redesign its allocator in advance.

## Recommendation and decision boundary

TCC currently looks like the shortest route to a self-contained guest compiler
that links the existing runtime and emits native executables. That assessment
comes from its complete pipeline and the narrow successful object/archive
links, not a claim that its source or libc port is simplest.

Chibicc becomes more attractive if we want to own and develop a readable compiler
backend, or plan to port guest binutils anyway. Lacc is worth a deeper audit if
direct ELF object output plus a separate linker is an acceptable split. Cproc
is a stronger candidate when a multi-tool guest development environment is in
scope. A compiler-plus-linker combination must preserve license notices and
be audited as a whole; borrowing TCC's linker does not make its file/runtime
adaptation disappear.

The current plan pursues TCC's self-contained pipeline with native Pyxis I/O and
P1F output. Follow its [decision checkpoints](tcc-port.md#pr-worklist) before
implementation. Revisit this comparison if that requires a broad linker rewrite
or bending OS interfaces around the compiler; a different frontend still needs
a complete guest toolchain.
