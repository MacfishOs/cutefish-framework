#include "directorymodel.h"

#include "backgroundrelease.h"
#include "task.h"

#include <QHash>
#include <QSet>

namespace Cutefish::IO {

void DirectoryModel::setDirLister(DirectoryLister *lister)
{
    if (m_lister)
        disconnect(m_lister, nullptr, this, nullptr);

    beginResetModel();
    m_lister = lister;
    m_items = lister->items();
    m_rowsValid = false;
    endResetModel();

    connect(lister, &DirectoryLister::itemsChanged, this, &DirectoryModel::synchronize);
    connect(lister, &DirectoryLister::directoryChanged, this, [this] {
        beginResetModel();
        releaseInBackground(std::exchange(m_items, {}));
        m_rowsValid = false;
        endResetModel();
    });
}

FileItem DirectoryModel::itemForIndex(const QModelIndex &idx) const
{
    return idx.isValid() && idx.row() < m_items.size() ? m_items.at(idx.row()) : FileItem();
}

QModelIndex DirectoryModel::indexForUrl(const QUrl &url) const
{
    if (!m_rowsValid) {
        releaseInBackground(std::exchange(m_rowsByUrl, {}));
        m_rowsByUrl.reserve(m_items.size());
        for (int row = 0; row < m_items.size(); ++row)
            m_rowsByUrl.insert(m_items[row].url(), row);
        m_rowsValid = true;
    }

    return index(m_rowsByUrl.value(url, -1), 0);
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
        case OriginalPath:
            return item.originalPath();
        case DeletionTime:
            return item.deletionTime();
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

    if (m_items.isEmpty()) {
        if (!incoming.isEmpty()) {
            beginInsertRows({}, 0, incoming.size() - 1);
            m_items = incoming;
            m_rowsValid = false;
            endInsertRows();
        }
        return;
    }

    // A refresh usually lists the same files in the same order.
    const int common = qMin(m_items.size(), incoming.size());
    int same = 0;

    while (same < common && m_items.at(same).url() == incoming.at(same).url())
        ++same;

    if (same == m_items.size() && same == incoming.size()) {
        releaseInBackground(std::exchange(m_items, incoming));
        emit dataChanged(index(0, 0), index(m_items.size() - 1, ColumnCount - 1));
        return;
    }

    // Files added or removed in one place leave the same files at both ends.
    int tail = 0;
    while (tail < common - same
           && m_items.at(m_items.size() - 1 - tail).url()
                   == incoming.at(incoming.size() - 1 - tail).url())
        ++tail;

    const int oldEnd = m_items.size() - tail;
    const int newEnd = incoming.size() - tail;

    QSet<QUrl> between;
    between.reserve(newEnd - same);
    for (int i = same; i < newEnd; ++i)
        between.insert(incoming.at(i).url());

    bool moved = false;
    for (int row = same; row < oldEnd && !moved; ++row)
        moved = between.contains(m_items.at(row).url());

    // No file in between kept its row: replace those rows, and the model mirrors
    // the listing without matching every URL.
    if (!moved) {
        FileItemList released;

        if (oldEnd > same) {
            beginRemoveRows({}, same, oldEnd - 1);
            for (int row = same; row < oldEnd; ++row)
                released.append(std::move(m_items[row]));
            m_items.remove(same, oldEnd - same);
            m_rowsValid = false;
            endRemoveRows();
        }

        if (newEnd > same) {
            beginInsertRows({}, same, newEnd - 1);
            releaseInBackground(std::exchange(m_items, incoming));
            m_rowsValid = false;
            endInsertRows();
        } else {
            releaseInBackground(std::exchange(m_items, incoming));
        }

        releaseInBackground(std::move(released));

        if (!m_items.isEmpty())
            emit dataChanged(index(0, 0), index(m_items.size() - 1, ColumnCount - 1));
        return;
    }

    QHash<QUrl, qsizetype> positions;
    positions.reserve(incoming.size());
    for (qsizetype i = 0; i < incoming.size(); ++i)
        positions.insert(incoming.at(i).url(), i);

    // Names that are not UTF-8 can decode to one text, so two files can share a URL;
    // their rows then match the last file with it, never by position.
    const bool unique = positions.size() == incoming.size();
    if (!unique)
        same = 0;

    // The rows replaced or removed, freed off this thread.
    FileItemList released;
    released.reserve(m_items.size());

    // Where each row's file is in the new listing, or -1 once it is gone.
    QList<qsizetype> sources(m_items.size());
    for (int row = 0; row < m_items.size(); ++row)
        sources[row] = row < same ? row : positions.value(m_items.at(row).url(), -1);

    for (int last = m_items.size() - 1; last >= 0;) {
        if (sources[last] >= 0) {
            --last;
            continue;
        }

        int first = last;

        while (first > 0 && sources[first - 1] < 0)
            --first;

        beginRemoveRows({}, first, last);
        for (int row = first; row <= last; ++row)
            released.append(std::move(m_items[row]));
        m_items.remove(first, last - first + 1);
        sources.remove(first, last - first + 1);
        m_rowsValid = false;
        endRemoveRows();
        last = first - 1;
    }

    QList<bool> kept(incoming.size(), false);
    for (int row = 0; row < m_items.size(); ++row) {
        kept[sources[row]] = true;
        released.append(std::exchange(m_items[row], incoming.at(sources[row])));
    }

    releaseInBackground(std::move(released));

    if (!m_items.isEmpty())
        emit dataChanged(index(0, 0), index(m_items.size() - 1, ColumnCount - 1));

    FileItemList added;
    for (qsizetype i = 0; i < incoming.size(); ++i) {
        if (!kept[unique ? i : positions.value(incoming.at(i).url())])
            added.append(incoming.at(i));
    }

    if (!added.isEmpty()) {
        const int first = m_items.size();
        beginInsertRows({}, first, first + added.size() - 1);
        m_items.append(added);
        m_rowsValid = false;
        endInsertRows();
    }

    releaseInBackground(std::move(positions));
}

} // namespace Cutefish::IO
