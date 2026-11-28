# LiteCode notices

LiteCode itself is licensed under Apache License 2.0. Binary distributions use
Qt 6 dynamically under LGPL-3.0 and include unmodified replaceable Qt libraries.
Recipients may replace the `Qt6*.dll` files in the package's `bin` directory
with interface-compatible builds using the same filenames. Corresponding Qt
6.11.2 source is available from <https://download.qt.io/archive/qt/6.11/6.11.2/>.
The package includes the LGPL-3.0 and incorporated GPL-3.0 texts under `licenses/`.

The editor uses Scintilla 5.6.4 and Lexilla 5.5.1 under their permissive licenses.
Their copyright and permission notice is included as `licenses/SCINTILLA.txt` and
the source archives are recorded in `THIRD_PARTY.md` and `SBOM.spdx.json`.

The terminal emulator uses libvterm 0.3.3 under the MIT License. Its copyright
and license text is included as `licenses/LIBVTERM-MIT.txt`.

Workspace text search uses the unmodified ripgrep 15.2.0 executable, Copyright
(c) 2015 Andrew Gallant, under the MIT License. Its license text is included as
`licenses/RIPGREP-MIT.txt`.

Optional automatic encoding detection uses the native uchardet 0.0.8 library
under MPL-1.1. Text conversion uses Qt Core5Compat and small native lookup tables;
LiteCode embeds no JavaScript runtime for encoding support. Binary packages include
`licenses/UCHARDET-MPL-1.1.txt`.

Selected Visual Studio Code Codicons are Copyright Microsoft Corporation and
contributors, licensed under CC BY 4.0. LiteCode vendors color-adapted copies from
Codicons 0.0.45. See `licenses/CODICONS-CC-BY-4.0.txt` and `THIRD_PARTY.md`.
The active-breakpoint stack-frame SVGs come from Microsoft's `vscode-icons`
under the same CC BY 4.0 license; their original light/dark colors are retained.

Theme-icon labels also embed the unmodified Codicons 0.0.46-40 font and glyph
mapping from Microsoft. See `licenses/CODICONS-FONT-LICENSE.txt` (including
Jason Long's Git-logo attribution), `licenses/VSCODE-MIT.txt`, `THIRD_PARTY.md`
and `SBOM.spdx.json`.

Explorer file icons use the Visual Studio Code Seti icon theme / Seti UI under the
MIT License. See `licenses/SETI-UI-MIT.txt`, `THIRD_PARTY.md`, and `SBOM.spdx.json`.

LiteCode's light and dark workbench and syntax palettes adapt selected colors
from Visual Studio Code Light Modern/Dark Modern and their included Light+/Dark+
themes, Copyright Microsoft Corporation and contributors, under the MIT License.
See `licenses/VSCODE-MIT.txt` and `THIRD_PARTY.md`.

Selected native workbench SVG controls are adapted from Lucide Icons, Copyright
Lucide Icons and Contributors, under the ISC License. Some Lucide icons are
derived from Feather Icons under the MIT License. See
`licenses/LUCIDE-ISC.txt` and `THIRD_PARTY.md`.

The native extension SDK's snippet builders adapt Visual Studio Code's
`snippetString.ts` behavior, Copyright Microsoft Corporation, under the MIT
License. See `licenses/VSCODE-MIT.txt` and the pinned source in `THIRD_PARTY.md`.

The Codex extension packages the unmodified OpenAI Codex native app-server and its
companion tools, Copyright 2025 OpenAI, under Apache-2.0. See
`licenses/CODEX-APACHE-2.0.txt`, `licenses/CODEX-NOTICE.txt`, and `THIRD_PARTY.md`.
The macOS Codex runtime also includes zsh; its notices are in
`licenses/CODEX-ZSH.txt`. Bundled Codex search resources retain ripgrep's MIT license.
Other developer tools such as Git, clangd and Claude Code remain separate tools
that can be installed and run from LiteCode's terminal.

Native extension event delivery and cancellation primitives adapt behavior from
Visual Studio Code's pinned `event.ts` and `cancellation.ts`, Copyright Microsoft
Corporation, under the MIT License. See `licenses/VSCODE-MIT.txt` and the pinned
sources in `THIRD_PARTY.md`.

Native extension Quick Pick/input-box options and asynchronous validation adapt Visual
Studio Code's pinned Quick Input behavior, Copyright Microsoft Corporation,
under the MIT License. See `licenses/VSCODE-MIT.txt` and `THIRD_PARTY.md`.

Native extension status-bar options, coalescing, message stacking and ordering
adapt Visual Studio Code's pinned status-bar behavior, Copyright Microsoft
Corporation, under the MIT License. See `licenses/VSCODE-MIT.txt` and `THIRD_PARTY.md`.

Native extension URI validation, handler registration and buffered activation
adapt Visual Studio Code's pinned URI behavior, Copyright Microsoft Corporation,
under the MIT License. See `licenses/VSCODE-MIT.txt` and `THIRD_PARTY.md`.

Native extension window-state delivery and input/progress activity timing adapt
Visual Studio Code's pinned window/activity behavior, Copyright Microsoft
Corporation, under the MIT License. See `licenses/VSCODE-MIT.txt` and `THIRD_PARTY.md`.

Native chat participant properties, followup-provider contracts and queued
metadata/disposal behavior adapt Visual Studio Code's pinned chat host behavior,
Copyright Microsoft Corporation, under the MIT License. See
`licenses/VSCODE-MIT.txt` and `THIRD_PARTY.md`.

Native text-editor edit collection, marker-operation metadata, range-overlap
validation and workbench surrogate-range adjustment adapt Visual Studio Code's pinned editor behavior, Copyright
Microsoft Corporation, under the MIT License. See `licenses/VSCODE-MIT.txt`
and `THIRD_PARTY.md`.

Native editor selection recovery and UTF-16 marker adjustment adapt Visual
Studio Code's pinned cursor and interval-tree behavior, Copyright Microsoft
Corporation, under the MIT License. See `licenses/VSCODE-MIT.txt` and
`THIRD_PARTY.md`.

Native TCP and named-pipe/Unix-socket debug adapter transports and manifest
executable selection follow Visual Studio Code's pinned implementation, Copyright Microsoft Corporation,
under the MIT License. See `licenses/VSCODE-MIT.txt` and `THIRD_PARTY.md`.

Native debug descriptor types, factory resolution, configuration-provider
contracts, two-phase resolution and debugServer precedence
adapt Visual Studio Code's pinned extension-host behavior, Copyright Microsoft
Corporation, under the MIT License. See `licenses/VSCODE-MIT.txt` and `THIRD_PARTY.md`.
