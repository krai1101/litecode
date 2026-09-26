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

Optional developer tools such as Git, clangd, Codex, and Claude Code are not
bundled or integrated into LiteCode. They can be installed separately and run
from LiteCode's terminal.
