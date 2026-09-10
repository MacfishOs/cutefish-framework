#include "directorymodel.h"
#include "task.h"
#include <QSet>

namespace Cutefish::IO {
void DirectoryModel::setDirLister(DirectoryLister *lister)
{
    if (m_lister)
        disconnect(m_lister, nullptr, this, nullptr);
    beginResetModel();
    m_lister = lister;
    m_items = lister->items();
    endResetModel();
    connect(lister, &DirectoryLister::itemsChanged, this, &DirectoryModel::synchronize);
    connect(lister, &DirectoryLister::directoryChanged, this, [this] {
        beginResetModel();
        m_items.clear();
        endResetModel();
    });
}

FileItem DirectoryModel::itemForIndex(const QModelIndex &idx) const
{
    return idx.isValid() && idx.row() < m_items.size() ? m_items.at(idx.row()) : FileItem();
}

QModelIndex DirectoryModel::indexForUrl(const QUrl &url) const
{
    for (int row = 0; row < m_items.size(); ++row)
        if (m_items[row].url() == url)
            return index(row, 0);
    return {};
}

QModelIndex DirectoryModel::index(int row, int column, const QModelIndex &parent) const
{
    return !parent.isValid() && row >= 0 && row < m_items.size() && column >= 0
                    && column < ColumnCount
            ? createIndex(row, column)
            : QModelIndex();
}

QVariant DirectoryModel::data(const QModelIndex &idx, int role) const
{
    const auto item = itemForIndex(idx);
    if (item.isNull())
        return {};
    if (role == FileItemRole)
        return QVariant::fromValue(item);
    if (role == ChildCountRole)
        return -1;
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (idx.column()) {
        case Name:
            return item.name();
        case Size:
            return item.size();
        case ModifiedTime:
            return item.time(FileItem::ModificationTime);
        case Type:
            return item.mimetype();
        default:
            return {};
        }
    }
    return {};
}

bool DirectoryModel::setData(const QModelIndex &idx, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !idx.isValid())
        return false;
    const auto name = value.toString();
    if (name.isEmpty() || name == "." || name == ".." || name.contains('/')
        || name.contains(QChar(0)))
        return false;
    const auto item = itemForIndex(idx);
    if (!item.isLocalFile())
        return false;
    const auto destination =
            QUrl::fromLocalFile(QFileInfo(item.localPath()).absolutePath() + '/' + name);
    auto *task = TaskController::instance()->submit(
            {Operation::Rename, {item.url()}, destination, ConflictPolicy::Ask});
    connect(task, &Task::result, this, [this](Task *done) {
        if (done->error())
            emit error(done->errorString());
    });
    return true;
}

Qt::ItemFlags DirectoryModel::flags(const QModelIndex &idx) const
{
    if (!idx.isValid())
        return Qt::ItemIsDropEnabled;
    auto flags =
            Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsEditable;
    if (itemForIndex(idx).isDir())
        flags |= Qt::ItemIsDropEnabled;
    return flags;
}

QMimeData *DirectoryModel::mimeData(const QModelIndexList &indexes) const
{
    auto *data = new QMimeData;
    QList<QUrl> urls;
    for (const auto &idx : indexes) {
        const auto item = itemForIndex(idx);
        if (!item.isNull() && !urls.contains(item.url()))
            urls.append(item.url());
    }
    data->setUrls(urls);
    return data;
}

void DirectoryModel::synchronize()
{
    const auto incoming = m_lister->items();
    QSet<QUrl> urls;
    for (const auto &item : incoming)
        urls.insert(item.url());
    for (int row = m_items.size() - 1; row >= 0; --row) {
        if (!urls.contains(m_items[row].url())) {
            beginRemoveRows({}, row, row);
            m_items.removeAt(row);
            endRemoveRows();
        }
    }
    QHash<QUrl, int> existing;
    for (int row = 0; row < m_items.size(); ++row)
        existing.insert(m_items[row].url(), row);
    for (const auto &item : incoming) {
        if (existing.contains(item.url())) {
            const int row = existing.value(item.url());
            m_items[row] = item;
            emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
        } else {
            const int row = m_items.size();
            beginInsertRows({}, row, row);
            m_items.append(item);
            endInsertRows();
        }
    }
}
} // namespace Cutefish::IO
