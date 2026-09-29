# Selecting existing build artifacts

[Independent build bundles](../development/build-bundles.md) are implemented. The next reuse
step should let a developer consume successful CI work without manually finding
and extracting every component, particularly after a port change has already
been built in CI.

- Begin with explicit artifact selection and a local cache. Decide the Forgejo
  run/artifact selector, authentication source, retention behavior and missing-
  artifact fallback before implementation. Do not embed repository tokens in
  bundles or update source checkouts as a side effect of `make run`.
- Match exact source inputs, compiler/build settings and SDK dependency identity.
  A clean checkout can be behind upstream; clean/dirty alone is not a cache key.
  Define component-specific input fingerprints before sharing artifacts across
  different workflow runs or reusing an SDK after unrelated root commits.
- Local edits rebuild the affected components and dependents; unrelated bundles
  remain reusable. Distinguish legitimate rebuilds after a TCC patch from an
  unchanged local build. Do not weaken dependency checks just to avoid work.
- Show which components were downloaded, reused or built, and why a candidate
  was rejected. Document offline behavior. Consider automatic selection only
  once the explicit path is dependable.

This is build-product reuse, not a package manager or a cross-repository dispatch
system. The latter remains a separate owner-managed integration.
