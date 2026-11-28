# Python Debugger for LiteCode

A native C++/Qt extension using LiteCode's shared extension Debug APIs and Microsoft's
debugpy. Python is installed by the user. debugpy is supplied inside this extension;
no pip installation or workspace adapter script is required.

Import this package through Extensions, then Enable. Open a saved `.py` file in a
workspace and press F5. Select Python Debugger and Python File. Run → Open
Configurations creates a standard `.vscode/launch.json` when needed:

```json
{
  "version": "0.2.0",
  "configurations": [{
    "name": "Python Debugger: Python File",
    "type": "debugpy",
    "request": "launch",
    "program": "${file}",
    "console": "integratedTerminal"
  }]
}
```

Use `module` instead of `program` for `python -m` debugging. `args` is an array of
literal arguments. `python` can be an executable path or an array containing the
executable and interpreter arguments. `debugAdapterPython` overrides the Python
used to host debugpy. `envFile`, `env`, `cwd`, `justMyCode`, `stopOnEntry`, Django,
Flask, Jinja, subprocess and path-mapping options are passed to debugpy as normal.
The default environment file is `${workspaceFolder}/.env`; explicit `env` values
override it. Environment files are parsed as data, never executed as shell code.

Interpreter selection uses the workspace selection, `python.defaultInterpreterPath`,
workspace virtual environments, `VIRTUAL_ENV`, Windows PEP 514 registration, then
PATH. Use **Python Debugger: Select Interpreter** to enter an executable or virtual
environment directory. This package requires Python 3.10 or newer.

For remote attach, start a debugpy DAP listener on the target yourself and use
`"request": "attach", "connect": {"host": "127.0.0.1", "port": 5678}`. That
mode needs no local Python. Local `listen` and `processId` modes use a spawned
adapter; process injection additionally depends on debugpy's platform-specific
native helpers and OS permissions. The portable macOS/Linux source package does
not include compiled process-injection helpers or Cython speedups. Launch and
socket attach use debugpy's portable Python implementation.

Implementation references: Microsoft's `vscode-python-debugger` adapter factory
and configuration providers, mapped to LiteCode's native SDK rather than loading
the VS Code JavaScript extension. No language-specific logic is added to the IDE.

Windows workflows are checked locally. macOS/Linux use the same extension code;
their runtime verification belongs to the platform CI runs.
