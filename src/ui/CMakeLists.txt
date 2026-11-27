add_library(litecode_ui STATIC
    DiffEditorWidget.cpp
    DiffEditorWidget.h
    styles/DiffEditorStyle.cpp
    styles/SourceControlStyle.cpp
    styles/RunDebugStyle.cpp
    ExtensionsPanel.cpp
    ExtensionsPanel.h
    extensions/NativeViewService.h extensions/NativeViewService.cpp
    extensions/NativeViewWidget.h extensions/NativeViewWidget.cpp
    extensions/NativeMenu.h extensions/NativeMenu.cpp
    components/MenuPainting.cpp
    extensions/NativeImageView.h extensions/NativeImageView.cpp
    extensions/NativeListDelegate.h
    extensions/NativeViewReconciler.h extensions/NativeViewReconciler.cpp
    extensions/NativeHoverGroup.h extensions/NativeHoverGroup.cpp
    extensions/NativeDialog.h extensions/NativeDialog.cpp
    extensions/NativeCodeBlock.h extensions/NativeCodeBlock.cpp
    extensions/ExtensionDetailsPage.h extensions/ExtensionDetailsPage.cpp
    extensions/NativeContentRow.h extensions/NativeContentRow.cpp
    extensions/NativeTextEdit.h extensions/NativeTextEdit.cpp extensions/NativeViewTextState.h
    extensions/NativeMarkdownView.h extensions/NativeMarkdownView.cpp
    extensions/NativeInlineCode.h extensions/NativeInlineCode.cpp
    extensions/NativeTranscriptView.h extensions/NativeTranscriptView.cpp
    extensions/NativeViewScrollState.h extensions/NativeViewScrollState.cpp
    SidebarService.h SidebarService.cpp SidebarResize.cpp
    SidebarDragController.h SidebarDragController.cpp
    components/PanelHeader.h components/PanelHeader.cpp
    SourceControlPanel.cpp
    SourceControlPanel.h
    RunDebugPanel.cpp
    debug/RunDebugStart.cpp
    RunDebugPanel.h
    debug/DebugInspectorWidget.cpp
    debug/DebugInspectorWidget.h
    debug/DebugInspectorDelegates.h debug/DebugInspectorDelegates.cpp
    debug/DebugConfigurationPicker.cpp
    debug/DebugConfigurationPicker.h
    debug/DebugTerminalLaunch.cpp
    debug/DebugTerminalLaunch.h
    debug/DebugTerminalController.cpp
    debug/DebugTerminalController.h
    GitHistoryWidget.cpp
    GitHistoryWidget.h
    GitReferenceBadges.cpp
    GitReferenceBadges.h
    GitBranchDialog.cpp
    GitBranchDialog.h
    GitBranchOperationsDialog.cpp
    GitBranchOperationsDialog.h
    GitWorktreeDialog.cpp
    GitWorktreeDialog.h
    GitCloneProgressDialog.cpp
    GitCloneProgressDialog.h
    GitPreferencesMenu.cpp
    GitPreferencesMenu.h
    GitActionsMenu.cpp
    GitActionsMenu.h
    GitTagRecoveryDialog.cpp
    GitTagRecoveryDialog.h
    GitTagDialog.cpp
    GitTagDialog.h
    GitTransferDialog.cpp
    GitTransferDialog.h
    GitStashDialog.cpp
    GitStashDialog.h
    GitRepositoryPicker.cpp
    GitRepositoryPicker.h
    GitCredentialBroker.cpp
    GitCredentialBroker.h
    chat/ParticipantChatPanel.cpp
    chat/ParticipantChatPanel.h
    chat/ParticipantResponseView.cpp
    chat/ParticipantResponseView.h
    resources/resources.qrc
    components/DescriptiveMenuAction.cpp
    components/Controls.cpp
    components/TreeDisclosure.cpp components/TreeDisclosure.h
    components/ElidingButton.cpp components/ElidingButton.h
    components/WorkbenchTabs.cpp
    components/WorkbenchTabs.h
    components/Controls.h
    components/IconLabelButton.cpp
    components/IconLabelButton.h
    components/SidebarView.cpp
    components/SidebarView.h
    components/ActionBar.cpp
    components/ActionBar.h
    components/ComponentStyle.cpp
    components/ComponentStyle.h
    components/ComponentTokens.cpp
    components/ComponentTokens.h
    ConfirmationDialog.cpp
    ConfirmationDialog.h
    components/DialogChrome.cpp
    components/DialogChrome.h
    components/DialogHeader.cpp
    components/DialogHeader.h
    components/EditorBreadcrumb.cpp
    components/EditorBreadcrumb.h
    components/EditorFindReplaceController.cpp
    components/EditorFindReplaceController.h
    components/FindTextInput.h
    commands/CommandRegistry.cpp
    commands/CommandRegistry.h
    controllers/ExplorerController.cpp
    controllers/ExplorerController.h
    controllers/EditorSessionController.cpp
    controllers/EditorSessionController.h
    controllers/WorkbenchController.cpp
    controllers/WorkbenchController.h
    explorer/ExplorerTreeView.cpp
    explorer/ExplorerTreeView.h
    explorer/ExplorerFileSystemModel.cpp
    explorer/ExplorerFileSystemModel.h
    EditorArea.cpp
    extensions/WorkbenchDocumentSaveService.h
    extensions/WorkbenchDocumentSaveService.cpp
    extensions/WorkbenchDocumentOpenService.h
    extensions/WorkbenchDocumentOpenService.cpp
    extensions/WorkbenchDiffCommand.h
    extensions/WorkbenchDiffCommand.cpp
    extensions/VirtualDocumentUpdates.h
    extensions/VirtualDocumentUpdates.cpp
    extensions/DetachedDocumentWatcher.h
    extensions/DetachedDocumentWatcher.cpp
    extensions/WorkbenchWorkspaceEditService.h
    extensions/WorkbenchWorkspaceEditService.cpp
    extensions/WorkspaceEditReview.h
    extensions/WorkspaceEditReviewDialog.h
    extensions/WorkspaceEditReviewDialog.cpp
    extensions/WorkspaceEditReviewService.h
    extensions/WorkspaceEditReviewService.cpp
    extensions/TextEditorEditPreparation.h
    extensions/TextEditorEditPreparation.cpp
    extensions/EditorSelectionTracking.h
    extensions/EditorSelectionTracking.cpp
    extensions/EditorSelectionPreparation.h
    extensions/EditorSelectionPreparation.cpp
    EditorArea.h
    EditorFontDialog.cpp
    EditorFontDialog.h
    SettingsDialog.cpp
    SettingsDialog.h
    settings/SettingsModel.cpp
    settings/SettingsModel.h
    FileIconTheme.cpp
    FileIconTheme.h
    KeyboardShortcutsDialog.cpp
    KeyboardShortcutsDialog.h
    MainWindow.cpp
    extensions/OutputPanel.cpp
    extensions/LogLevelCommands.cpp
    extensions/OutputPanel.h
    extensions/WindowMessageService.cpp
    extensions/WindowMessageService.h
    extensions/ProgressNotification.cpp
    extensions/ProgressNotification.h
    extensions/ViewProgressService.cpp
    extensions/ViewProgressService.h
    extensions/StatusProgressService.cpp
    extensions/StatusProgressService.h
    extensions/FileDialogService.cpp
    extensions/FileDialogService.h
    extensions/InputBoxService.h
    extensions/InputBoxService.cpp
    extensions/QuickPickService.h
    extensions/QuickPickService.cpp
    extensions/StatusBarService.h
    extensions/StatusBarService.cpp
    extensions/ExternalUriService.h
    extensions/ExternalUriService.cpp
    extensions/NativeUriDispatcher.h
    extensions/NativeUriDispatcher.cpp
    extensions/NativeUriBroker.h
    extensions/NativeUriBroker.cpp
    extensions/WindowStateService.h
    extensions/WindowStateService.cpp
    extensions/UriConfirmationDialog.h
    extensions/UriConfirmationDialog.cpp
    components/ComponentAccessibility.h
    components/ComponentAccessibility.cpp
    components/CssColor.h
    components/CssColor.cpp
    extensions/FileDialogDefaults.cpp
    extensions/FileDialogDefaults.h
    components/ModalDialogQueue.cpp
    components/ModalDialogQueue.h
    MainWindow.h
    components/CircularProgress.h components/CircularProgress.cpp
    components/ElidingLabel.h components/ElidingLabel.cpp
    components/ShimmerLabel.h components/ShimmerLabel.cpp
    components/ChoiceSlider.h
    components/ChoiceSlider.cpp
    components/PopupSurface.cpp
    components/PopupSurface.h
    components/Tooltip.cpp components/Tooltip.h
    components/QuickInputDialog.cpp
    components/QuickInputDialog.h
    components/TerminalFindControls.cpp
    components/TerminalFindControls.h
    QuickOpenDialog.cpp
    QuickOpenDialog.h
    styles/ComponentStyleSheets.h
    styles/OverlayStyle.cpp
    styles/EditorChromeStyle.cpp
    styles/ExplorerStyle.cpp
    styles/ScrollbarStatusStyle.cpp
    styles/IconLabelButtonStyle.cpp
    styles/SettingsDialogVisuals.cpp
    styles/SettingsDialogVisuals.h
    styles/StyleTemplate.cpp
    styles/StyleTemplate.h
    styles/TerminalPanelStyle.cpp
    styles/WorkbenchStyle.cpp
    Theme.cpp
    Theme.h
    TransientScrollBars.cpp
    TransientScrollBars.h
    TerminalPanel.cpp
    TerminalPanel.h
    TerminalSessionDelegate.cpp
    TerminalSessionDelegate.h
    TerminalView.cpp
    TerminalView.h
    ThemedIcon.cpp
    ThemedIcon.h
    WorkbenchTitleBar.cpp
    WorkbenchTitleBar.h
    WorkbenchChrome.cpp
    WorkbenchChrome.h
    WindowOutline.cpp
    WindowOutline.h
    WorkbenchDock.cpp
    WorkbenchDock.h
)

target_link_libraries(litecode_ui
    PUBLIC
        litecode_core
        litecode_extensions
        litecode_workspace
        litecode_terminal
        litecode_debug
        litecode_ui_primitives
        Qt6::Widgets
        Qt6::Network
    PRIVATE
        litecode_editor
        Qt6::Concurrent
        Qt6::Svg
        litecode_project_options
)
target_include_directories(litecode_ui PUBLIC ${PROJECT_SOURCE_DIR}/src)
set_property(TARGET litecode_ui PROPERTY AUTOGEN_PARALLEL 1)

add_custom_target(litecode_ui_component_check
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckUiComponents.cmake
    COMMENT "Checking LiteCode UI component boundaries"
    VERBATIM
)
add_dependencies(litecode_ui litecode_ui_component_check)

target_sources(litecode_ui PRIVATE GitOutputChannel.cpp GitOutputChannel.h)
