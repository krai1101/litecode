# LiteCode benchmarks

`litecode.benchmark.editor5mb` creates deterministic fixtures at runtime. It
opens a 5 MiB file through the production editor, applies incremental edits to
1, 5, and 20 MiB documents, and searches a generated 2,000-file workspace.

Run the benchmark from a Release build on an otherwise idle machine:

```powershell
ctest --test-dir build/release -R benchmark -V
```

The benchmark verifies that loading returns the expected contents, edits report
only the inserted delta, and workspace search respects its result and traversal
limits. It reports elapsed times without treating a busy CI runner as a product
regression. Record reproducible measurements separately when making a performance
claim.
