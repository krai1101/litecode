# JavaScript Debugger

Install Node.js and make it available on PATH. Import this extension folder in LiteCode, enable it, and press F5 with a saved JavaScript file open. Choose Node.js, then Launch Program. Attach to Node.js connects to an existing inspector endpoint (default port 9229).

Use `type: "node"` in a saved launch configuration. For compiled TypeScript, set `program` to the emitted JavaScript and `outFiles` to the emitted files with source maps. The extension handles one Node.js target per debug session.

The bundled adapter is [Microsoft's standalone JavaScript debugger](https://github.com/microsoft/vscode-js-debug/releases/tag/v1.140.1). Its license ships with the adapter.
