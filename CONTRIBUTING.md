# Contributing to LiteCode

Thanks for contributing. Please open an issue before starting substantial work so
the design and scope can be agreed first.

## Development setup

Use C++20, Qt 6 Widgets, CMake, and Ninja. Follow the platform setup in the
[README](README.md). Keep workbench UI in `src/ui` and move stateful or process
logic behind small interfaces in the relevant module.

## Before opening a pull request

Keep changes focused, add regression tests where practical, and run:

```powershell
.\scripts\build-windows.cmd Release
.\scripts\format.ps1 -Check
.\scripts\check-architecture.ps1
```

On Linux or macOS, run `cmake --preset release`, `cmake --build --preset release`,
and `ctest --preset release` with Qt 6.11.2 available through `CMAKE_PREFIX_PATH`.

## Contribution rules

- Do not add dependencies without recording the exact version, license, source,
  and redistribution duties in [THIRD_PARTY.md](THIRD_PARTY.md).
- Do not add GPL-only dependencies or web UI runtimes.
- Keep the UI thread non-blocking; use asynchronous `QProcess` signals or
  cancellable workers for external processes.
- Do not include credentials, private files, generated build outputs, or release
  binaries in a pull request.

By contributing, you agree that your contribution is licensed under the Apache
License 2.0 in [LICENSE](LICENSE).
