# LiteCode

[![Build and test](https://github.com/krai1101/litecode/actions/workflows/build.yml/badge.svg)](https://github.com/krai1101/litecode/actions/workflows/build.yml)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

LiteCode is a lightweight native desktop IDE built with C++ and Qt Widgets. It
provides the everyday editing loop—files, editing, search, and an integrated
terminal—without Electron, Chromium, a web view, or an embedded AI client.

> **Beta software:** LiteCode is still in beta. Use it cautiously, keep backups
> of important work, and report problems you encounter.

## The repository

This repository is where LiteCode is developed in the open. It contains the
application source, tests, build tooling, release workflow, and the documents
needed to build and redistribute LiteCode.

## Features

- Workspace Explorer with create, rename, system-trash deletion, and Git decorations.
- Tabbed Scintilla editor with syntax highlighting, find/replace, and session
  restore.
- Quick Open, command palette, and cancellable workspace text search.
- Multiple native terminal sessions: ConPTY on Windows and PTY on Unix-like
  systems.
- Optional external Git and `clangd` integration.
- Native Light Modern and Dark Modern themes.

Settings for editor font and file encoding apply across all folders. Resetting a setting
restores its default. Existing `<workspace>/.litecode/settings.ini` files are ignored.

Developer tools such as Codex, Claude Code, Git, and language servers remain
separate processes. Install and run them normally from LiteCode's terminal.

## Downloads

Prebuilt packages for Windows, Linux, and macOS are published on the
[Releases page](https://github.com/krai1101/litecode/releases).

Windows receives the complete package smoke test. Linux and macOS packages are
built and tested for pull requests, manual checks, and versioned releases.

## Build from source

LiteCode requires CMake 3.24+, Ninja, a C++20 compiler, and Qt 6.11.2 with the
Core, Core5Compat, Concurrent, GUI, Widgets, SVG, and Test modules.

On Windows, install Visual Studio 2022 with the Desktop development with C++
workload, then run:

```powershell
.\scripts\install-qt-windows.ps1
.\scripts\build-windows.cmd Release
.\run-litecode.cmd
```

The Qt installer script downloads the required open-source archives directly
from Qt's public repository; it does not require a Qt account or credentials.

For Linux and macOS setup, testing, formatting, and contribution requirements,
see [CONTRIBUTING.md](CONTRIBUTING.md).

## Contributing

Bug reports, feature requests, and pull requests are welcome. Read
[CONTRIBUTING.md](CONTRIBUTING.md) before submitting a change.

## Security

Please report vulnerabilities privately as described in [SECURITY.md](SECURITY.md).
Do not open a public issue for a security report.

## License and notices

LiteCode is available under the [Apache License 2.0](LICENSE).

Qt 6.11.2 is dynamically linked under LGPLv3. Third-party licenses, notices,
source records, and redistribution information are in [NOTICE.md](NOTICE.md),
[THIRD_PARTY.md](THIRD_PARTY.md), and [SBOM.spdx.json](SBOM.spdx.json).
