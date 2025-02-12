/*
* All or portions of this file Copyright (c) Amazon.com, Inc. or its affiliates or
* its licensors.
*
* For complete copyright and license terms please see the LICENSE at the root of this
* distribution (the "License"). All use of this software is governed by the License,
* or, if provided, by the license below or the license accompanying this file. Do not
* remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*
*/

#include "AssetTreeItem.h"

#include <QFileIconProvider>
#include <QIcon>
#include <QVariant>

namespace AssetProcessor
{

    AssetTreeItemData::AssetTreeItemData(const AZStd::string& assetDbName, QString name, bool isFolder, const AZ::Uuid& uuid) :
        m_assetDbName(assetDbName),
        m_name(name),
        m_isFolder(isFolder),
        m_uuid(uuid)
    {
        QFileInfo fileInfo(name);
        m_extension = fileInfo.completeSuffix();
    }

    AssetTreeItem::AssetTreeItem(AZStd::shared_ptr<AssetTreeItemData> data, AssetTreeItem* parentItem) :
        m_data(data),
        m_parent(parentItem)
    {

    }

    AssetTreeItem::~AssetTreeItem()
    {
    }

    AssetTreeItem* AssetTreeItem::CreateChild(AZStd::shared_ptr<AssetTreeItemData> data)
    {
        m_childItems.emplace_back(new AssetTreeItem(data, this));
        return m_childItems.back().get();
    }

    AssetTreeItem* AssetTreeItem::GetChild(int row) const
    {
        if (row < 0 || row >= getChildCount())
        {
            return nullptr;
        }
        return m_childItems.at(row).get();
    }

    void AssetTreeItem::EraseChild(AssetTreeItem* child)
    {
        for (auto& item : m_childItems)
        {
            if (item.get() == child)
            {
                m_childItems.erase(&item);
                break;
            }
        }
    }

    int AssetTreeItem::getChildCount() const
    {
        return static_cast<int>(m_childItems.size());
    }

    int AssetTreeItem::GetRow() const
    {
        if (m_parent)
        {
            int index = 0;
            for (const auto& item : m_parent->m_childItems)
            {
                if (item.get() == this)
                {
                    return index;
                }
                ++index;
            }
        }
        return 0;
    }

    int AssetTreeItem::GetColumnCount() const
    {
        return static_cast<int>(AssetTreeColumns::Max);
    }

    QVariant AssetTreeItem::GetDataForColumn(int column) const
    {
        if (column < 0 || column >= GetColumnCount() || !m_data)
        {
            return QVariant();
        }
        switch (column)
        {
            case static_cast<int>(AssetTreeColumns::Name):
                return m_data->m_name;
            case static_cast<int>(AssetTreeColumns::Extension):
                if (m_data->m_isFolder)
                {
                    return QVariant();
                }
                return m_data->m_extension;
            default:
                AZ_Warning("AssetProcessor", false, "Unhandled AssetTree column %d", column);
                break;
        }
        return QVariant();
    }

    QIcon AssetTreeItem::GetIcon(const QFileIconProvider& iconProvider) const
    {
        if (!m_data)
        {
            return QIcon();
        }
        if (m_data->m_isFolder)
        {
            return iconProvider.icon(QFileIconProvider::Folder);
        }
        else
        {
            return iconProvider.icon(QFileIconProvider::File);
        }
    }

    AssetTreeItem* AssetTreeItem::GetParent() const
    {
        return m_parent;
    }

    AssetTreeItem* AssetTreeItem::GetChildFolder(QString folder) const
    {
        for (const auto& item : m_childItems)
        {
            if (!item->m_data ||
                !item->m_data->m_isFolder)
            {
                continue;
            }
            if (item->m_data->m_name == folder)
            {
                return item.get();
            }
        }
        return nullptr;
    }

}
