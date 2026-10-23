include(FetchContent)

FetchContent_Declare(scintilla_source
    # SourceForge hosts the official release archive and provides a more reliable CI download
    # endpoint than the project's small web server.
    URL https://downloads.sourceforge.net/project/scintilla/scintilla/5.6.4/scintilla564.tgz
    URL_HASH SHA256=c62e19449ed066867fde4edc6e3864f56d16f893c9f7582792fcf27dd733a48e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(scintilla_source)

set(_scintilla_root "${scintilla_source_SOURCE_DIR}")

add_library(scintilla_qt STATIC
    ${_scintilla_root}/qt/ScintillaEditBase/PlatQt.cpp
    ${_scintilla_root}/qt/ScintillaEditBase/PlatQt.h
    ${_scintilla_root}/qt/ScintillaEditBase/ScintillaEditBase.cpp
    ${_scintilla_root}/qt/ScintillaEditBase/ScintillaEditBase.h
    ${_scintilla_root}/qt/ScintillaEditBase/ScintillaQt.cpp
    ${_scintilla_root}/qt/ScintillaEditBase/ScintillaQt.h
    ${_scintilla_root}/src/XPM.cxx
    ${_scintilla_root}/src/ViewStyle.cxx
    ${_scintilla_root}/src/UndoHistory.cxx
    ${_scintilla_root}/src/UniqueString.cxx
    ${_scintilla_root}/src/UniConversion.cxx
    ${_scintilla_root}/src/Style.cxx
    ${_scintilla_root}/src/Selection.cxx
    ${_scintilla_root}/src/ScintillaBase.cxx
    ${_scintilla_root}/src/RunStyles.cxx
    ${_scintilla_root}/src/RESearch.cxx
    ${_scintilla_root}/src/PositionCache.cxx
    ${_scintilla_root}/src/PerLine.cxx
    ${_scintilla_root}/src/MarginView.cxx
    ${_scintilla_root}/src/LineMarker.cxx
    ${_scintilla_root}/src/KeyMap.cxx
    ${_scintilla_root}/src/Indicator.cxx
    ${_scintilla_root}/src/Geometry.cxx
    ${_scintilla_root}/src/EditView.cxx
    ${_scintilla_root}/src/Editor.cxx
    ${_scintilla_root}/src/EditModel.cxx
    ${_scintilla_root}/src/Document.cxx
    ${_scintilla_root}/src/Decoration.cxx
    ${_scintilla_root}/src/DBCS.cxx
    ${_scintilla_root}/src/ContractionState.cxx
    ${_scintilla_root}/src/CharClassify.cxx
    ${_scintilla_root}/src/CharacterType.cxx
    ${_scintilla_root}/src/CharacterCategoryMap.cxx
    ${_scintilla_root}/src/ChangeHistory.cxx
    ${_scintilla_root}/src/CellBuffer.cxx
    ${_scintilla_root}/src/CaseFolder.cxx
    ${_scintilla_root}/src/CaseConvert.cxx
    ${_scintilla_root}/src/CallTip.cxx
    ${_scintilla_root}/src/AutoComplete.cxx
)

target_include_directories(scintilla_qt
    PUBLIC
        ${_scintilla_root}/include
        ${_scintilla_root}/qt/ScintillaEditBase
        ${_scintilla_root}/src
)
target_compile_definitions(scintilla_qt
    PUBLIC EXPORT_IMPORT_API=
    PRIVATE
    SCINTILLA_QT=1
)
target_link_libraries(scintilla_qt
    PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Core5Compat
)
set_target_properties(scintilla_qt PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
