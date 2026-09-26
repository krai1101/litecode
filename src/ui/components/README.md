# LiteCode UI components

This directory is LiteCode's native Qt equivalent of VS Code's
`src/vs/base/browser/ui` layer. Feature widgets compose these controls; they do not
redefine their visual states.

## Rules

- Use `Button`, `IconButton`, `Input`, `List`, `Menu`, `ActionBar`, `DialogChrome`,
  `DialogHeader`, and `PopupSurface` instead of constructing equivalent raw Qt controls.
- Choose semantic variants and states (`Primary`, `Secondary`, `Ghost`, `Danger`,
  `InputState::Error`) rather than feature-specific colors.
- Component colors and dimensions come from `ComponentTokens`; feature styles contain
  layout-specific rules only.
- Do not call `setStyleSheet()` from feature code.
- Native operating-system file pickers remain exempt.

The `litecode_ui_component_check` build target enforces the raw-control and inline-style
boundaries.
