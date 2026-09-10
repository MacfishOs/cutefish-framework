#pragma once
#include "directorylister.h"
#include <QAbstractItemModel>
#include <QMimeData>

namespace Cutefish::IO {
class DirectoryModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Column {
        Name,
        Size,
        ModifiedTime,
        Type,
        Permissions,
        Owner,
        Group,
        ColumnCount
    };

    enum Role {
        FileItemRole = Qt::UserRole + 1,
        ChildCountRole
    };

    explicit DirectoryModel(QObject *parent = nullptr)
        : QAbstractItemModel(parent)
    {
    }

    void setDirLister(DirectoryLister *lister);

    DirectoryLister *dirLister() const { return m_lister; }

    FileItem itemForIndex(const QModelIndex &index) const;
    QModelIndex indexForUrl(const QUrl &url) const;
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;

    QModelIndex parent(const QModelIndex &) const override { return {}; }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_items.size();
    }

    int columnCount(const QModelIndex & = {}) const override { return ColumnCount; }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;

    QStringList mimeTypes() const override { return {"text/uri-list"}; }

    Qt::DropActions supportedDragActions() const override
    {
        return Qt::CopyAction | Qt::MoveAction;
    }

signals:
    void error(const QString &message);

private:
    void synchronize();
    DirectoryLister *m_lister = nullptr;
    FileItemList m_items;
};
} // namespace Cutefish::IO
