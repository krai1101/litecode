#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <functional>

namespace litecode::workspace {

enum class WorkspaceNameIssue {
    None,
    Empty,
    DotName,
    PathSeparator,
    ControlCharacter,
    WindowsInvalidCharacter,
    WindowsTrailingDotOrSpace,
    WindowsReservedDevice,
};

struct WorkspaceNameValidation final {
    WorkspaceNameIssue issue{WorkspaceNameIssue::None};
    QString message;

    [[nodiscard]] bool valid() const noexcept { return issue == WorkspaceNameIssue::None; }
};

class WorkspaceService final : public QObject {
    Q_OBJECT

  public:
    using TrashOperation = std::function<bool(const QString&, QString*)>;

    explicit WorkspaceService(QObject* parent = nullptr);
    explicit WorkspaceService(TrashOperation trashOperation, QObject* parent = nullptr);

    [[nodiscard]] QString rootPath() const;
    [[nodiscard]] bool hasWorkspace() const;
    bool openFolder(const QString& folderPath, QString* errorMessage = nullptr);
    void closeWorkspace();
    bool createFile(const QString& parentDirectory, const QString& name,
                    QString* createdPath = nullptr, QString* errorMessage = nullptr);
    bool createDirectory(const QString& parentDirectory, const QString& name,
                         QString* createdPath = nullptr, QString* errorMessage = nullptr);
    bool renamePath(const QString& path, const QString& newName, QString* renamedPath = nullptr,
                    QString* errorMessage = nullptr);
    bool movePath(const QString& path, const QString& destinationDirectory,
                  QString* movedPath = nullptr, QString* errorMessage = nullptr);
    bool movePathToTrash(const QString& path, QString* errorMessage = nullptr);
    bool movePathToTrash(const QString& path, const QByteArray& expectedIdentity,
                         QString* errorMessage = nullptr);
    bool removePath(const QString& path, const QByteArray& expectedIdentity,
                    QString* errorMessage = nullptr);
    [[nodiscard]] bool containsPath(const QString& path) const;
    [[nodiscard]] bool containsEntryPath(const QString& path) const;
    [[nodiscard]] static QString canonicalContainmentPath(const QString& path);
    [[nodiscard]] static QByteArray entryIdentity(const QString& path);
    [[nodiscard]] static bool entryExists(const QString& path);
    [[nodiscard]] static WorkspaceNameValidation validateEntryName(const QString& name);

  signals:
    void workspaceChanged(const QString& rootPath);

  private:
    QString rootPath_;
    TrashOperation trashOperation_;
};

} // namespace litecode::workspace

Q_DECLARE_METATYPE(litecode::workspace::WorkspaceNameIssue)
