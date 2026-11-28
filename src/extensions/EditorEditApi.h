#pragma once
#include "extensions/TextApi.h"

namespace litecode::extensions {
// Stable vscode.TextEditorEdit members. Selection is already a Range subtype.
// delete_ is the native spelling of the C++ keyword delete.
class TextEditorEdit {
  public:
    virtual ~TextEditorEdit() = default;
    virtual void replace(const Position& location, const QString& value) = 0;
    virtual void replace(const Range& location, const QString& value) = 0;
    virtual void insert(const Position& location, const QString& value) = 0;
    virtual void delete_(const Range& location) = 0;
    virtual void setEndOfLine(EndOfLine endOfLine) = 0;
};
} // namespace litecode::extensions
