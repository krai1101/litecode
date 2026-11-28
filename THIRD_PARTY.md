# Third-party dependencies

This file is the dependency approval record. Shipped dependencies are pinned to
the exact version, archive hash, or source commit recorded below. Build-only and
user-installed tools state their supported minimum and the version used for the
current Windows qualification environment. Distribution packaging must include the
corresponding notices, license texts, and source information required by each
dependency.

| Component | Version | Use | License | Distribution policy |
|---|---:|---|---|---|
| Visual Studio Code selection tracking | commit `5e418d6d41f7464a64877411e8ae3b20669d5c71` | Native UTF-16 marker adjustment and selection recovery after extension edits | MIT | Preserve Microsoft attribution and `licenses/VSCODE-MIT.txt`. Sources: `src/vs/editor/common/model/intervalTree.ts` and `src/vs/editor/common/cursor/oneCursor.ts` in the pinned `https://github.com/microsoft/vscode` tree. |
| Visual Studio Code network debug adapter behavior | commit `5e418d6d41f7464a64877411e8ae3b20669d5c71` | Native TCP and named-pipe/Unix-socket stream lifecycle, provider-owned server separation and platform manifest executable selection | MIT | Preserve Microsoft attribution and `licenses/VSCODE-MIT.txt`. Source: `https://github.com/microsoft/vscode/blob/5e418d6d41f7464a64877411e8ae3b20669d5c71/src/vs/workbench/contrib/debug/node/debugAdapter.ts`. |
| Visual Studio Code debug provider behavior | commit `5e418d6d41f7464a64877411e8ae3b20669d5c71` | Native typed adapter descriptors, inline routing, session context/events/control, tracker hooks and collection timeout, configuration generation and two-phase provider resolution, null/undefined cancellation, debugServer precedence and terminal startup/command preparation | MIT | Preserve Microsoft attribution and `licenses/VSCODE-MIT.txt`. Sources: pinned `src/vscode-dts/vscode.d.ts`, `src/vs/workbench/api/common/extHostDebugService.ts`, `src/vs/workbench/api/node/extHostDebugService.ts` and `src/vs/workbench/contrib/debug/node/terminals.ts` in `https://github.com/microsoft/vscode`. |
| Visual Studio Code editor-edit behavior | commit `5e418d6d41f7464a64877411e8ae3b20669d5c71` | Native edit builder, transaction metadata and overlap validation | MIT | Preserve Microsoft attribution and `licenses/VSCODE-MIT.txt`. Source: `https://github.com/microsoft/vscode/blob/5e418d6d41f7464a64877411e8ae3b20669d5c71/src/vs/workbench/api/common/extHostTextEditor.ts`; public contracts in the pinned `src/vscode-dts/vscode.d.ts`. |
| Visual Studio Code chat participant behavior | commit `5e418d6d41f7464a64877411e8ae3b20669d5c71` | Native chat participant properties, provider contracts, coalesced metadata and disposal | MIT | Preserve Microsoft attribution and `licenses/VSCODE-MIT.txt`. Source: `https://github.com/microsoft/vscode/blob/5e418d6d41f7464a64877411e8ae3b20669d5c71/src/vs/workbench/api/common/extHostChatAgents2.ts`; public contracts in the pinned `src/vscode-dts/vscode.d.ts`. |
| Visual Studio Code window/activity behavior | commit `5e418d6d41f7464a64877411e8ae3b20669d5c71` | Native window-state snapshots/events, input idle timing and progress activity holds | MIT | Preserve Microsoft attribution and `licenses/VSCODE-MIT.txt`. Sources: `src/vs/workbench/api/common/extHostWindow.ts`, `src/vs/workbench/api/browser/mainThreadWindow.ts`, `src/vs/workbench/services/userActivity/browser/domActivityTracker.ts`, `src/vs/workbench/services/userActivity/common/userActivityService.ts` and `src/vs/workbench/services/progress/browser/progressService.ts` in the pinned `https://github.com/microsoft/vscode` source tree. |
| Visual Studio Code Codicons font / glyph mapping | 0.0.46-40 | Shared native theme-icon labels | CC BY 4.0; Git-logo glyph CC BY 3.0; CSS mapping header MIT | Unmodified font SHA-256 `77f902557231f95c4c71ae517d09a1a842753656f5f665086b3e5ad8095538da`; CSS SHA-256 `8bf3737ca6a3529c9f01e7e95839b72e4fd037dfae49c8b92e9e7bab43db2408`. Preserve Microsoft attribution, Jason Long Git-logo attribution, `licenses/CODICONS-FONT-LICENSE.txt` and `licenses/VSCODE-MIT.txt`. Source: `https://www.npmjs.com/package/@vscode/codicons/v/0.0.46-40`. |
| Qt | 6.11.2 | Core, GUI, Widgets, SVG, tests, JSON, process management | LGPL-3.0 or GPL/commercial alternatives | Use only LGPL-eligible modules and dynamic linking. Include notices, license, replaceable libraries, installation information, and a public corresponding-source location. Source: `https://download.qt.io/archive/qt/6.11/6.11.2/`. |
| CMake | 3.24+; 4.3.2 qualified | Build configuration | BSD-3-Clause | Build-time only; not linked or redistributed. Source: `https://github.com/Kitware/CMake`. |
| Ninja | 1.11+; 1.13.2 qualified | Build execution | Apache-2.0 | Build-time only; not linked or redistributed. Source: `https://github.com/ninja-build/ninja`. |
| clang-format | 18.1.8+ | C++ source formatting | Apache-2.0 with LLVM exception | Developer tool only; not linked or redistributed. |
| Scintilla | 5.6.4, SHA-256 `c62e19449ed066867fde4edc6e3864f56d16f893c9f7582792fcf27dd733a48e` | Native editor engine | Scintilla permissive license | Pinned HTTPS source archive fetched by CMake from `https://downloads.sourceforge.net/project/scintilla/scintilla/5.6.4/scintilla564.tgz`. Preserve its copyright and permission notices. |
| Lexilla | 5.5.1, SHA-256 `5c147f927927b35c55c630ef3a9affd5708400a3cad80447cc1043ab9a816f31` | Syntax lexers | Scintilla permissive license | Pinned HTTPS source archive fetched by CMake from `https://github.com/ScintillaOrg/lexilla/archive/refs/tags/rel-5-5-1.zip`. Preserve its copyright and permission notices. |
| libvterm | 0.3.3, SHA-256 `0babe3ab42c354925dadedE90d352f054aa9c4ae6842ea803a20c9741e172e56` | VT/xterm parsing, terminal screen state, keyboard and mouse protocol encoding | MIT | Pinned source archive fetched from `https://github.com/neovim/libvterm/releases/tag/v0.3.3`. Statically linked; include the copyright and MIT text from `licenses/LIBVTERM-MIT.txt` in source and binary distributions. The archived GitHub mirror is used only for this audited release snapshot. |
| ripgrep | 15.2.0; Windows SHA-256 `71b2fef860abe467217a538ff31de02f5258807c0129f771846f87bd029aafc5`; Linux SHA-256 `33e15bcf1624b25cdd2a55813a47a2f95dbe126268203e76aa6a585d1e7b149c`; macOS x86_64 SHA-256 `af7825fcc69a2afc7a7aea55fc9af90e26421d8f20fe59df32e233c0b8a231c1`; macOS arm64 SHA-256 `3750b2e93f37e0c692657da574d7019a101c0084da05a790c83fd335bad973e4` | Bundled asynchronous workspace text search executable | MIT (upstream also offers Unlicense) | Download official unmodified release archives from `https://github.com/BurntSushi/ripgrep/releases/tag/15.2.0`, verify the platform archive hash in CMake, and ship the executable beside LiteCode. Preserve copyright and the MIT text in `licenses/RIPGREP-MIT.txt`. |
| uchardet | 0.0.8, source archive SHA-256 `e97a60cfc00a1c147a674b097bb1422abd9fa78a2d9ce3f3fdcc2e78a34ac5f0` | Optional native automatic text-encoding detection | MPL-1.1 | Pinned, hash-verified C++ source fetched from Debian's upstream-source archive and statically linked. Include `licenses/UCHARDET-MPL-1.1.txt`. Upstream project: `https://www.freedesktop.org/wiki/Software/uchardet/`. |
| Visual Studio Code Codicons | 0.0.45, commit `dbb6555160650624bb1719d6264910b1cbe528f2` | Vendored workbench SVG icons | CC BY 4.0 | Only selected SVG assets are redistributed. Preserve Microsoft attribution and `licenses/CODICONS-CC-BY-4.0.txt`; mark local color-only adaptations. Source: `https://github.com/microsoft/vscode-codicons`. |
| Visual Studio Code snippet builders | commit `5e418d6d41f7464a64877411e8ae3b20669d5c71` | Native C++ adaptation of stable snippet string construction | MIT | Preserve Microsoft attribution and `licenses/VSCODE-MIT.txt`. Source: `https://github.com/microsoft/vscode/blob/5e418d6d41f7464a64877411e8ae3b20669d5c71/src/vs/workbench/api/common/extHostTypes/snippetString.ts`. |
| Visual Studio Code extension primitives | commit `5e418d6d41f7464a64877411e8ae3b20669d5c71` | Native C++ adaptation of synchronous nested event delivery and lazy cancellation lifecycle | MIT | Preserve Microsoft attribution and `licenses/VSCODE-MIT.txt`. Sources: `https://github.com/microsoft/vscode/blob/5e418d6d41f7464a64877411e8ae3b20669d5c71/src/vs/base/common/event.ts` and `https://github.com/microsoft/vscode/blob/5e418d6d41f7464a64877411e8ae3b20669d5c71/src/vs/base/common/cancellation.ts`. |
| Visual Studio Code status-bar behavior | commit `5e418d6d41f7464a64877411e8ae3b20669d5c71` | Native C++/Qt adaptation of stable status items, coalesced updates, message lifetimes and ordering | MIT | Preserve Microsoft attribution and `licenses/VSCODE-MIT.txt`. Sources: `https://github.com/microsoft/vscode/blob/5e418d6d41f7464a64877411e8ae3b20669d5c71/src/vs/workbench/api/common/extHostStatusBar.ts` and `https://github.com/microsoft/vscode/blob/5e418d6d41f7464a64877411e8ae3b20669d5c71/src/vs/workbench/browser/parts/statusbar/statusbarModel.ts`. |
| Visual Studio Code URI behavior | commit `5e418d6d41f7464a64877411e8ae3b20669d5c71` | Native C++/Qt adaptation of URI validation, extension-scoped handlers, buffered activation and confirmation | MIT | Preserve Microsoft attribution and `licenses/VSCODE-MIT.txt`. Sources: `https://github.com/microsoft/vscode/blob/5e418d6d41f7464a64877411e8ae3b20669d5c71/src/vs/workbench/api/common/extHostWindow.ts`, `https://github.com/microsoft/vscode/blob/5e418d6d41f7464a64877411e8ae3b20669d5c71/src/vs/workbench/api/common/extHostUrls.ts` and `https://github.com/microsoft/vscode/blob/5e418d6d41f7464a64877411e8ae3b20669d5c71/src/vs/workbench/services/extensions/browser/extensionUrlHandler.ts`. |
| Visual Studio Code Quick Input behavior | commit `5e418d6d41f7464a64877411e8ae3b20669d5c71` | Native C++/Qt adaptation of stable input/picker options, original item handles, cancellation and asynchronous validation/acceptance | MIT | Preserve Microsoft attribution and `licenses/VSCODE-MIT.txt`. Sources: `https://github.com/microsoft/vscode/blob/5e418d6d41f7464a64877411e8ae3b20669d5c71/src/vs/workbench/api/common/extHostQuickOpen.ts` and `https://github.com/microsoft/vscode/blob/5e418d6d41f7464a64877411e8ae3b20669d5c71/src/vs/platform/quickinput/browser/quickInputController.ts`. |
| Visual Studio Code product icons (`vscode-icons`) | Light and dark `debug-breakpoint-stackframe-active.svg` | Active breakpoint stack-frame marker | CC BY 4.0 | Original SVG shapes and colors retained; preserve Microsoft attribution and `licenses/CODICONS-CC-BY-4.0.txt`. Source: `https://github.com/microsoft/vscode-icons/tree/main/icons`. |
| Visual Studio Code Seti icon theme / Seti UI | VS Code commit `6ebcc0a377ae04e130212f34df551c8340cf966a`; Seti UI commit `2d6c5e68b4ded73c92dac291845ee44e1182d511`; source WOFF SHA-256 `b127762058f89b37b08d76185e3b0558f5c28859e39a3840a27cb28b9739d6e2`; Windows-compatible TTF SHA-256 `b1e0623f0492615e2d9eec11a3d6429de914404fd500d86401c4dd6860f5a62a`; mapping SHA-256 `2088843e6f00a81870ed2890132fb658cf33406177a5dd58c9c7001e5184ca2a` | Native Explorer file icons and file-association mapping | MIT | Vendored `seti.ttf` is a lossless container conversion of the official `seti.woff`; preserve Microsoft and Jesse Weed copyright notices and `licenses/SETI-UI-MIT.txt`. Sources: `https://github.com/microsoft/vscode/tree/6ebcc0a377ae04e130212f34df551c8340cf966a/extensions/theme-seti` and `https://github.com/jesseweed/seti-ui/tree/2d6c5e68b4ded73c92dac291845ee44e1182d511`. |
| FontTools | 4.59.0 | One-time WOFF-to-TTF container conversion for the vendored Seti font; not linked or shipped | MIT | Build-time conversion tool only. Source: `https://github.com/fonttools/fonttools/tree/4.59.0`. Preserve its license when redistributing the tool itself; LiteCode distributes only the converted MIT-licensed Seti font. |
| Visual Studio Code Light Modern and Dark Modern themes | commit `b10759526092db723a7217e7627ae309d74719f4` | Visual reference for LiteCode-owned workbench palettes and compact Lexilla role colors | MIT | Only selected color values are adapted into LiteCode's own JSON files. No TextMate grammars, tokenizer, or VS Code code are shipped or linked. Preserve Microsoft copyright and `licenses/VSCODE-MIT.txt`. Source: `https://github.com/microsoft/vscode/tree/b10759526092db723a7217e7627ae309d74719f4/extensions/theme-defaults/themes`. |
| Lucide Icons | commit `4aec3f892fd6c23063bc2fead83c899b5d412b1c` | Selected native workbench SVG controls | ISC; selected Feather-derived icons also MIT | Only selected SVG assets are redistributed. Preserve the Lucide and Feather notices in `licenses/LUCIDE-ISC.txt`. Source: `https://github.com/lucide-icons/lucide/tree/4aec3f892fd6c23063bc2fead83c899b5d412b1c`. |
| install-qt-action | v4.3.1, commit `48d3ad6db93f3627c8ee7a0454bc6f3744f7e730` | Installs Qt 6.11.2 in hosted Linux/macOS CI | MIT | Build-only GitHub Action pinned to an immutable revision; not linked or redistributed. |
| actions/checkout | v6.1.0, commit `d23441a48e516b6c34aea4fa41551a30e30af803` | Authenticated source checkout in hosted CI | MIT | Build-only GitHub Action pinned to an immutable revision; not linked or redistributed. |
| actions/upload-artifact | v4.6.2, commit `ea165f8d65b6e75b540449e92b4886f43607fa02` | Uploads CI packages | MIT | Build-only GitHub Action pinned to an immutable revision; not linked or redistributed. |
| actions/download-artifact | v4.3.0, commit `d3f86a106a0bac45b974a628896c90dbdf5c8093` | Collects verified packages for tagged releases | MIT | Build-only GitHub Action pinned to an immutable revision; not linked or redistributed. |
| softprops/action-gh-release | v2.6.2, commit `3bb12739c298aeb8a4eeaf626c5b8d85266b0e65` | Publishes packages for version tags | MIT | Release-only GitHub Action pinned to an immutable revision; granted write access only in the release job. |

## Native extension secret-storage dependencies

The macOS/Linux extension host links QtKeychain (BSD-3-Clause) and OpenSSL 3
(Apache-2.0); Windows uses the system DPAPI library. QtKeychain's fallback source
is pinned to 0.15.0, commit `ad7344c45a86a4f66cbafc4b081b5f7b876cb0b7`, and is
compiled statically against the application's Qt. Its upstream `COPYING` is
installed as `licenses/QTKEYCHAIN-BSD.txt`. An externally supplied Qt6Keychain
and OpenSSL installation must retain the notices required by those packages;
release packaging and SBOM attribution for the resolved versions still need
to be finalized before redistribution. Sources: [QtKeychain](https://github.com/frankosterfeld/qtkeychain/tree/ad7344c45a86a4f66cbafc4b081b5f7b876cb0b7)
and [OpenSSL](https://github.com/openssl/openssl).

## Codex chat runtime

OpenAI Codex 0.157.1 is redistributed unmodified from OpenAI's official npm
platform packages under Apache-2.0. The complete native vendor layout (including
code-mode host, sandbox resources and search executable) is preserved. No Node.js
launcher or JavaScript UI is used. Platform SHA-512 pins are in `extensions/codex/cmake/Runtime.cmake`.
Source and transitive dependency notices: https://github.com/openai/codex/tree/rust-v0.157.1.
The runtime includes MIT-licensed Ratatui-derived code, recorded in its NOTICE.
Bundled voice companion notices, licenses and source records remain alongside
the unmodified runtime under `codex/codex-resources/voice/`.

## OpenAI extension icon

The Codex package uses OpenAI's white Blossom SVG from the official Codex VS Code
extension, displayed as a transparent monochrome icon. This OpenAI-owned mark is separate from the
runtime's Apache-2.0 license and LiteCode's software license. Source and attribution
are recorded in `extensions/codex/assets/ATTRIBUTION.md` and included in the package.

## Rejected dependencies

- QScintilla: its no-cost license is GPLv3 and would impose a project-wide GPL decision.
- Qt WebEngine and Qt WebView: violate LiteCode's native, non-web UI requirement.
- GPL-only Qt modules: incompatible with the chosen dependency policy.

### Lucide icons (Codex extension)

The Codex extension packages Lucide SVG icons under `extensions/codex/assets/lucide`.
They are distributed under the ISC licence, included with their pinned source
revision in that directory. These assets are not linked into the editor resources.

## Python Debugger extension

The optional native Python Debugger extension bundles Microsoft debugpy 1.8.22,
including its vendored components and upstream notices. Runtime archives are pinned
by SHA-256 in `extensions/python-debugger/cmake/Debugpy.cmake`. They are copied into
the extension package, not the editor or the user's Python installation. The
upstream distribution's licence files are preserved under the package's `licenses`.

The Python Debugger icon comes from Microsoft's MIT-licensed
[vscode-python-debugger](https://github.com/microsoft/vscode-python-debugger)
repository; its licence is included in the package. Python itself is user-installed.

## Java and JavaScript Debugger extensions

The Java Debugger package contains the Microsoft Java Debug Server 0.53.2
([source and EPL-1.0 license](https://github.com/microsoft/java-debug/tree/0.53.2))
and Eclipse JDT LS 1.57.0
([source and EPL-2.0 license](https://github.com/eclipse-jdtls/eclipse.jdt.ls/tree/v1.57.0)).
The package preserves the upstream notices and the Eclipse bundles' embedded notices.
Users supply their own JDK.

The JavaScript Debugger package contains Microsoft's MIT-licensed
[standalone js-debug adapter 1.140.1](https://github.com/microsoft/vscode-js-debug/releases/tag/v1.140.1)
and preserves the license and bundled notices. Users supply their own Node.js runtime.
