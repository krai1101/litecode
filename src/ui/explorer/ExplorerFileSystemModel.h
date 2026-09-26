#pragma once

#include <QAbstractProxyModel>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHash>
#include <QPersistentModelIndex>

#include <memory>
#include <vector>

namespace litecode::ui {

// A filesystem proxy that can inject one transient child for New File/New Folder.
class ExplorerFileSystemModel final : public QAbstractProxyModel {
    Q_OBJECT

  public:
    enum Role { TemporaryEntryRole = Qt::UserRole + 1, DirectoryRole };

    explicit ExplorerFileSystemModel(QObject* parent = nullptr);
    QModelIndex mapToSource(const QModelIndex& proxyIndex) const override;
    QModelIndex mapFromSource(const QModelIndex& sourceIndex) const override;
    QModelIndex index(int row, int column,
                      const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool hasChildren(const QModelIndex& parent = QModelIndex()) const override;
    bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    void setFilter(QDir::Filters filters);
    void setReadOnly(bool readOnly);
    QModelIndex setRootPath(const QString& path);
    [[nodiscard]] QModelIndex index(const QString& path, int column = 0) const;
    [[nodiscard]] QString filePath(const QModelIndex& index) const;
    [[nodiscard]] QFileInfo fileInfo(const QModelIndex& index) const;
    [[nodiscard]] bool isDir(const QModelIndex& index) const;
    QModelIndex insertTemporaryEntry(const QModelIndex& parent, bool directory);
    void removeTemporaryEntry();
    [[nodiscard]] QModelIndex temporaryEntry() const;
    [[nodiscard]] bool isTemporaryEntry(const QModelIndex& index) const;

  signals:
    void directoryLoaded(const QString& path);
    void temporaryEntryInvalidated();

  private:
    struct Node {
        enum class Kind { Source, Temporary } kind{Kind::Source};
        QPersistentModelIndex source;
    };

    [[nodiscard]] Node* nodeForSource(const QModelIndex& sourceIndex) const;
    [[nodiscard]] QModelIndex sourceParent(const QModelIndex& proxyParent) const;
    [[nodiscard]] int proxyRowForSource(const QModelIndex& sourceIndex) const;
    [[nodiscard]] bool hasTemporaryChild(const QModelIndex& sourceParent) const;
    [[nodiscard]] int temporaryInsertionRow(const QModelIndex& sourceParent, bool directory) const;
    void connectSourceModel();
    void clearNodeCache();

    QFileSystemModel* files_{};
    mutable QHash<QPersistentModelIndex, Node*> sourceNodes_;
    mutable std::vector<std::unique_ptr<Node>> ownedNodes_;
    Node temporaryNode_{Node::Kind::Temporary, {}};
    QPersistentModelIndex temporaryParent_;
    int temporaryRow_{-1};
    bool temporaryDirectory_{false};
    QString temporaryName_;
    QModelIndexList layoutPersistentIndexes_;
    int pendingTemporaryRowDelta_{};
};

} // namespace litecode::ui
