# Python Debugger assets and implementation references

The Python Debugger icon is `icon.png` from Microsoft's MIT-licensed
https://github.com/microsoft/vscode-python-debugger at revision
`629d0aba4605d6dfb5358dff978dbbda4efe68ae`.
The accompanying licence is `VSCODE-PYTHON-DEBUGGER-MIT.txt`.

The native configuration providers and adapter descriptor factory follow the
contracts in that repository's `src/extension/debugger/configuration` and
`src/extension/debugger/adapter/factory.ts`, using LiteCode's C++ extension SDK.
The runtime is the unmodified debugpy distribution pinned in `cmake/Debugpy.cmake`;
its licence files are preserved in the extension package.
