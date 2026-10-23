#include "ui/explorer/ExplorerFileSystemModel.h"

#include <QDir>

#include <utility>

namespace litecode::ui {
namespace {

bool isSameOrDescendant(QModelIndex candidate, const QModelIndex& ancestor) {
    for (; candidate.isValid(); candidate = candidate.parent()) {
        if (candidate == ancestor)
            return true;
    }
    return false;
}

} // namespace

ExplorerFileSystemModel::ExplorerFileSystemModel(QObject* parent)
    : QAbstractProxyModel(parent), files_(new QFileSystemModel(this)) {
    setSourceModel(files_);
    connectSourceModel();
}

QModelIndex ExplorerFileSystemModel::mapToSource(const QModelIndex& proxyIndex) const {
    if (!proxyIndex.isValid() || proxyIndex.model() != this)
        return {};
    const auto* node = static_cast<const Node*>(proxyIndex.internalPointer());
    return node != nullptr && node->kind == Node::Kind::Source ? QModelIndex(node->source)
                                                               : QModelIndex{};
}

QModelIndex ExplorerFileSystemModel::mapFromSource(const QModelIndex& sourceIndex) const {
    if (!sourceIndex.isValid() || sourceIndex.model() != files_)
        return {};
    return createIndex(proxyRowForSource(sourceIndex), sourceIndex.column(),
                       nodeForSource(sourceIndex));
}

QModelIndex ExplorerFileSystemModel::index(int row, int column,
                                           const QModelIndex& parentIndex) const {
    if (row < 0 || column < 0 || column >= columnCount(parentIndex))
        return {};
    const QModelIndex sourceParentIndex = sourceParent(parentIndex);
    if (hasTemporaryChild(sourceParentIndex) && row == temporaryRow_)
        return createIndex(row, column, const_cast<Node*>(&temporaryNode_));
    const int sourceRow =
        row - (hasTemporaryChild(sourceParentIndex) && row > temporaryRow_ ? 1 : 0);
    return mapFromSource(files_->index(sourceRow, column, sourceParentIndex));
}

QModelIndex ExplorerFileSystemModel::parent(const QModelIndex& child) const {
    if (!child.isValid())
        return {};
    if (isTemporaryEntry(child))
        return mapFromSource(temporaryParent_);
    return mapFromSource(mapToSource(child).parent());
}

int ExplorerFileSystemModel::rowCount(const QModelIndex& parentIndex) const {
    if (parentIndex.column() > 0 || isTemporaryEntry(parentIndex))
        return 0;
    const QModelIndex sourceParentIndex = sourceParent(parentIndex);
    return files_->rowCount(sourceParentIndex) + (hasTemporaryChild(sourceParentIndex) ? 1 : 0);
}

int ExplorerFileSystemModel::columnCount(const QModelIndex& parentIndex) const {
    return files_->columnCount(sourceParent(parentIndex));
}

QVariant ExplorerFileSystemModel::data(const QModelIndex& proxyIndex, int role) const {
    if (isTemporaryEntry(proxyIndex)) {
        if (role == Qt::DisplayRole || role == Qt::EditRole)
            return proxyIndex.column() == 0 ? temporaryName_ : QString{};
        if (role == TemporaryEntryRole)
            return true;
        if (role == DirectoryRole)
            return temporaryDirectory_;
        return {};
    }
    if (role == Qt::DecorationRole)
        return {};
    if (role == TemporaryEntryRole)
        return false;
    if (role == DirectoryRole)
        return isDir(proxyIndex);
    return files_->data(mapToSource(proxyIndex), role);
}

bool ExplorerFileSystemModel::setData(const QModelIndex& proxyIndex, const QVariant& value,
                                      int role) {
    if (isTemporaryEntry(proxyIndex) && (role == Qt::EditRole || role == Qt::DisplayRole)) {
        temporaryName_ = value.toString();
        emit dataChanged(proxyIndex, proxyIndex, {Qt::DisplayRole, Qt::EditRole});
        return true;
    }
    return files_->setData(mapToSource(proxyIndex), value, role);
}

Qt::ItemFlags ExplorerFileSystemModel::flags(const QModelIndex& proxyIndex) const {
    if (isTemporaryEntry(proxyIndex))
        return Qt::ItemIsEnabled | Qt::ItemIsEditable;
    return files_->flags(mapToSource(proxyIndex));
}

bool ExplorerFileSystemModel::hasChildren(const QModelIndex& parentIndex) const {
    if (isTemporaryEntry(parentIndex))
        return false;
    return rowCount(parentIndex) > 0 || files_->hasChildren(sourceParent(parentIndex));
}

bool ExplorerFileSystemModel::canFetchMore(const QModelIndex& parentIndex) const {
    return !isTemporaryEntry(parentIndex) && files_->canFetchMore(sourceParent(parentIndex));
}

void ExplorerFileSystemModel::fetchMore(const QModelIndex& parentIndex) {
    if (!isTemporaryEntry(parentIndex))
        files_->fetchMore(sourceParent(parentIndex));
}

QVariant ExplorerFileSystemModel::headerData(int section, Qt::Orientation orientation,
                                             int role) const {
    return files_->headerData(section, orientation, role);
}

void ExplorerFileSystemModel::sort(int column, Qt::SortOrder order) { files_->sort(column, order); }
void ExplorerFileSystemModel::setFilter(QDir::Filters filters) { files_->setFilter(filters); }
void ExplorerFileSystemModel::setReadOnly(bool readOnly) { files_->setReadOnly(readOnly); }

QModelIndex ExplorerFileSystemModel::setRootPath(const QString& path) {
    return mapFromSource(files_->setRootPath(path));
}

QModelIndex ExplorerFileSystemModel::index(const QString& path, int column) const {
    return mapFromSource(files_->index(path, column));
}

QString ExplorerFileSystemModel::filePath(const QModelIndex& proxyIndex) const {
    return files_->filePath(mapToSource(proxyIndex));
}

QFileInfo ExplorerFileSystemModel::fileInfo(const QModelIndex& proxyIndex) const {
    return files_->fileInfo(mapToSource(proxyIndex));
}

bool ExplorerFileSystemModel::isDir(const QModelIndex& proxyIndex) const {
    return isTemporaryEntry(proxyIndex) ? temporaryDirectory_
                                        : files_->isDir(mapToSource(proxyIndex));
}

QModelIndex ExplorerFileSystemModel::insertTemporaryEntry(const QModelIndex& parentIndex,
                                                          bool directory) {
    removeTemporaryEntry();
    const QModelIndex sourceParentIndex = sourceParent(parentIndex);
    temporaryParent_ = sourceParentIndex;
    temporaryDirectory_ = directory;
    temporaryName_.clear();
    temporaryRow_ = temporaryInsertionRow(sourceParentIndex, directory);
    beginInsertRows(parentIndex, temporaryRow_, temporaryRow_);
    endInsertRows();
    return index(temporaryRow_, 0, parentIndex);
}

void ExplorerFileSystemModel::removeTemporaryEntry() {
    if (temporaryRow_ < 0)
        return;
    const QModelIndex proxyParent = mapFromSource(temporaryParent_);
    beginRemoveRows(proxyParent, temporaryRow_, temporaryRow_);
    temporaryRow_ = -1;
    temporaryParent_ = QPersistentModelIndex{};
    temporaryName_.clear();
    endRemoveRows();
}

QModelIndex ExplorerFileSystemModel::temporaryEntry() const {
    return temporaryRow_ < 0 ? QModelIndex{}
                             : index(temporaryRow_, 0, mapFromSource(temporaryParent_));
}

bool ExplorerFileSystemModel::isTemporaryEntry(const QModelIndex& proxyIndex) const {
    return proxyIndex.isValid() && proxyIndex.model() == this &&
           proxyIndex.internalPointer() == &temporaryNode_;
}

ExplorerFileSystemModel::Node*
ExplorerFileSystemModel::nodeForSource(const QModelIndex& sourceIndex) const {
    const QPersistentModelIndex key(sourceIndex);
    if (Node* existing = sourceNodes_.value(key, nullptr))
        return existing;
    auto node = std::make_unique<Node>();
    node->source = key;
    Node* result = node.get();
    ownedNodes_.push_back(std::move(node));
    sourceNodes_.insert(key, result);
    return result;
}

QModelIndex ExplorerFileSystemModel::sourceParent(const QModelIndex& proxyParent) const {
    return proxyParent.isValid() ? mapToSource(proxyParent) : QModelIndex{};
}

int ExplorerFileSystemModel::proxyRowForSource(const QModelIndex& sourceIndex) const {
    const bool shifted =
        hasTemporaryChild(sourceIndex.parent()) && sourceIndex.row() >= temporaryRow_;
    return sourceIndex.row() + (shifted ? 1 : 0);
}

bool ExplorerFileSystemModel::hasTemporaryChild(const QModelIndex& sourceParentIndex) const {
    return temporaryRow_ >= 0 && temporaryParent_ == sourceParentIndex;
}

int ExplorerFileSystemModel::temporaryInsertionRow(const QModelIndex& sourceParentIndex,
                                                   bool directory) const {
    if (directory)
        return 0;
    int row = 0;
    while (row < files_->rowCount(sourceParentIndex) &&
           files_->isDir(files_->index(row, 0, sourceParentIndex)))
        ++row;
    return row;
}

void ExplorerFileSystemModel::connectSourceModel() {
    connect(files_, &QFileSystemModel::directoryLoaded, this,
            &ExplorerFileSystemModel::directoryLoaded);
    connect(files_, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        const bool hadTemporaryEntry = temporaryRow_ >= 0;
        temporaryRow_ = -1;
        temporaryParent_ = QPersistentModelIndex{};
        temporaryName_.clear();
        if (hadTemporaryEntry)
            emit temporaryEntryInvalidated();
        beginResetModel();
        clearNodeCache();
    });
    connect(files_, &QAbstractItemModel::modelReset, this, [this] { endResetModel(); });
    connect(files_, &QAbstractItemModel::rowsAboutToBeInserted, this,
            [this](const QModelIndex& parent, int first, int last) {
                pendingTemporaryRowDelta_ = 0;
                int offset = 0;
                if (hasTemporaryChild(parent)) {
                    if (first <= temporaryRow_)
                        pendingTemporaryRowDelta_ = last - first + 1;
                    else
                        offset = 1;
                }
                beginInsertRows(mapFromSource(parent), first + offset, last + offset);
            });
    connect(files_, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex&, int, int) {
        temporaryRow_ += pendingTemporaryRowDelta_;
        pendingTemporaryRowDelta_ = 0;
        endInsertRows();
    });
    connect(files_, &QAbstractItemModel::rowsAboutToBeRemoved, this,
            [this](const QModelIndex& parent, int first, int last) {
                pendingTemporaryRowDelta_ = 0;
                int offset = 0;
                bool invalidatesTemporaryParent = false;
                if (temporaryRow_ >= 0) {
                    for (int row = first; row <= last; ++row) {
                        if (isSameOrDescendant(temporaryParent_, files_->index(row, 0, parent))) {
                            invalidatesTemporaryParent = true;
                            break;
                        }
                    }
                }
                if (invalidatesTemporaryParent) {
                    temporaryRow_ = -1;
                    temporaryParent_ = QPersistentModelIndex{};
                    temporaryName_.clear();
                    emit temporaryEntryInvalidated();
                }
                if (hasTemporaryChild(parent)) {
                    if (last < temporaryRow_)
                        pendingTemporaryRowDelta_ = -(last - first + 1);
                    else if (first >= temporaryRow_)
                        offset = 1;
                }
                beginRemoveRows(mapFromSource(parent), first + offset, last + offset);
            });
    connect(files_, &QAbstractItemModel::rowsRemoved, this, [this](const QModelIndex&, int, int) {
        temporaryRow_ += pendingTemporaryRowDelta_;
        pendingTemporaryRowDelta_ = 0;
        endRemoveRows();
    });
    connect(files_, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& topLeft, const QModelIndex& bottomRight,
                   const QList<int>& roles) {
                emit dataChanged(mapFromSource(topLeft), mapFromSource(bottomRight), roles);
            });
    connect(files_, &QAbstractItemModel::layoutAboutToBeChanged, this, [this] {
        layoutPersistentIndexes_ = persistentIndexList();
        emit layoutAboutToBeChanged();
    });
    connect(files_, &QAbstractItemModel::layoutChanged, this, [this] {
        QModelIndexList remappedIndexes;
        remappedIndexes.reserve(layoutPersistentIndexes_.size());
        for (const QModelIndex& oldIndex : std::as_const(layoutPersistentIndexes_)) {
            if (isTemporaryEntry(oldIndex)) {
                remappedIndexes.push_back(temporaryEntry());
                continue;
            }
            const auto* node = static_cast<const Node*>(oldIndex.internalPointer());
            remappedIndexes.push_back(node != nullptr && node->source.isValid()
                                          ? mapFromSource(node->source)
                                          : QModelIndex{});
        }
        changePersistentIndexList(layoutPersistentIndexes_, remappedIndexes);
        layoutPersistentIndexes_.clear();
        emit layoutChanged();
    });
}

void ExplorerFileSystemModel::clearNodeCache() {
    sourceNodes_.clear();
    ownedNodes_.clear();
}

} // namespace litecode::ui
