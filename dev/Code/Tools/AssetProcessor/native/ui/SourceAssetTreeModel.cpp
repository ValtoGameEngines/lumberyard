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

#include "SourceAssetTreeModel.h"
#include "AssetTreeItem.h"
#include "SourceAssetTreeItemData.h"

#include <AzCore/Component/TickBus.h>
#include <AzFramework/StringFunc/StringFunc.h>
#include <native/utilities/assetUtils.h>

#include <QDir>

namespace AssetProcessor
{

    SourceAssetTreeModel::SourceAssetTreeModel(QObject *parent) :
        AssetTreeModel(parent)
    {
        AzToolsFramework::AssetDatabase::AssetDatabaseNotificationBus::Handler::BusConnect();
    }

    SourceAssetTreeModel::~SourceAssetTreeModel()
    {
        AzToolsFramework::AssetDatabase::AssetDatabaseNotificationBus::Handler::BusDisconnect();
    }

    void SourceAssetTreeModel::ResetModel()
    {
        m_sourceToTreeItem.clear();
        m_sourceIdToTreeItem.clear();

        m_dbConnection.QuerySourceAndScanfolder(
            [&](AzToolsFramework::AssetDatabase::SourceAndScanFolderDatabaseEntry& sourceAndScanFolder)
        {
            AddOrUpdateEntry(sourceAndScanFolder, sourceAndScanFolder, true);
            return true; // return true to continue iterating over additional results, we are populating a container
        });
    }

    void SourceAssetTreeModel::AddOrUpdateEntry(
        const AzToolsFramework::AssetDatabase::SourceDatabaseEntry& source,
        const AzToolsFramework::AssetDatabase::ScanFolderDatabaseEntry& scanFolder,
        bool modelIsResetting)
    {
        const auto& existingEntry = m_sourceToTreeItem.find(source.m_sourceName);
        if (existingEntry != m_sourceToTreeItem.end())
        {
            AZStd::shared_ptr<SourceAssetTreeItemData> sourceItemData = AZStd::rtti_pointer_cast<SourceAssetTreeItemData>(existingEntry->second->GetData());

            // This item already exists, refresh the related data.
            sourceItemData->m_scanFolderInfo = scanFolder;
            sourceItemData->m_sourceInfo = source;
            QModelIndex existingIndexStart = createIndex(existingEntry->second->GetRow(), 0, existingEntry->second);
            QModelIndex existingIndexEnd = createIndex(existingEntry->second->GetRow(), existingEntry->second->GetColumnCount() - 1, existingEntry->second);
            dataChanged(existingIndexStart, existingIndexEnd);
            return;
        }


        AZStd::string fullPath = source.m_sourceName;

        // The source assets should look like they do on disk.
        // If the scan folder has an output prefix, strip it from the source file's path in the database, before
        // the scan folder path is prepended to the source file.
        if (!scanFolder.m_outputPrefix.empty())
        {
            AZStd::string prefixPath = scanFolder.m_outputPrefix;
            AzFramework::StringFunc::Append(prefixPath, AZ_CORRECT_DATABASE_SEPARATOR);
            AzFramework::StringFunc::Replace(fullPath, prefixPath.c_str(), "", false, true);
        }

        AzFramework::StringFunc::AssetDatabasePath::Join(scanFolder.m_scanFolder.c_str(), fullPath.c_str(), fullPath, false, true, false);

        // It's common for Lumberyard game projects and scan folders to be in a subfolder
        // of the engine install. To improve readability of the source files, strip out
        // that portion of the path if it overlaps.
        if (!m_assetRootSet)
        {
            m_assetRootSet = AssetUtilities::ComputeAssetRoot(m_assetRoot, nullptr);
        }
        if (m_assetRootSet)
        {
            AzFramework::StringFunc::Replace(fullPath, m_assetRoot.absolutePath().toUtf8(), "");
        }


        AZStd::vector<AZStd::string> tokens;
        AzFramework::StringFunc::Tokenize(fullPath.c_str(), tokens, AZ_CORRECT_DATABASE_SEPARATOR, false, true);

        if (tokens.empty())
        {
            AZ_Warning("AssetProcessor", false, "Source id %s has an invalid name: %s",
                source.m_sourceGuid.ToString<AZStd::string>().c_str(), source.m_sourceName.c_str());
            return;
        }

        QModelIndex newIndicesStart;

        AssetTreeItem* parentItem = m_root.get();
        AZStd::string fullFolderName;
        for (int i = 0; i < tokens.size() - 1; ++i)
        {
            AzFramework::StringFunc::AssetDatabasePath::Join(fullFolderName.c_str(), tokens[i].c_str(), fullFolderName);
            AssetTreeItem* nextParent = parentItem->GetChildFolder(tokens[i].c_str());
            if (!nextParent)
            {
                if (!modelIsResetting)
                {
                    QModelIndex parentIndex = parentItem == m_root.get() ? QModelIndex() : createIndex(parentItem->GetRow(), 0, parentItem);
                    beginInsertRows(parentIndex, parentItem->getChildCount(), parentItem->getChildCount());
                }
                nextParent = parentItem->CreateChild(SourceAssetTreeItemData::MakeShared(nullptr, nullptr, fullFolderName, tokens[i].c_str(), true));
                m_sourceToTreeItem[fullFolderName] = nextParent;
                // Folders don't have source IDs, don't add to m_sourceIdToTreeItem
                if (!modelIsResetting)
                {
                    endInsertRows();
                }
            }
            parentItem = nextParent;
        }

        if (!modelIsResetting)
        {
            QModelIndex parentIndex = parentItem == m_root.get() ? QModelIndex() : createIndex(parentItem->GetRow(), 0, parentItem);
            beginInsertRows(parentIndex, parentItem->getChildCount(), parentItem->getChildCount());
        }

        m_sourceToTreeItem[source.m_sourceName] =
            parentItem->CreateChild(SourceAssetTreeItemData::MakeShared(&source, &scanFolder, source.m_sourceName, tokens[tokens.size() - 1].c_str(), false));
        m_sourceIdToTreeItem[source.m_sourceID] = m_sourceToTreeItem[source.m_sourceName];
        if (!modelIsResetting)
        {
            endInsertRows();
        }
    }

    void SourceAssetTreeModel::OnSourceFileChanged(const AzToolsFramework::AssetDatabase::SourceDatabaseEntry& entry)
    {
        // Model changes need to be run on the main thread.
        AZ::SystemTickBus::QueueFunction([&, entry]()
        {
            m_dbConnection.QueryScanFolderBySourceID(entry.m_sourceID,
                [&, entry](AzToolsFramework::AssetDatabase::ScanFolderDatabaseEntry& scanFolder)
            {
                AddOrUpdateEntry(entry, scanFolder, false);
                return true;
            });
        });
    }

    void SourceAssetTreeModel::RemoveFoldersIfEmpty(AssetTreeItem* itemToCheck)
    {
        // Don't attempt to remove invalid items, non-folders, folders that still have items in them, or the root.
        if (!itemToCheck || !itemToCheck->GetData()->m_isFolder || itemToCheck->getChildCount() > 0 || !itemToCheck->GetParent())
        {
            return;
        }
        RemoveAssetTreeItem(itemToCheck);
    }

    void SourceAssetTreeModel::RemoveAssetTreeItem(AssetTreeItem* assetToRemove)
    {
        if (!assetToRemove)
        {
            return;
        }
        AssetTreeItem* parent = assetToRemove->GetParent();
        QModelIndex parentIndex = createIndex(parent->GetRow(), 0, parent);

        beginRemoveRows(parentIndex, assetToRemove->GetRow(), assetToRemove->GetRow());

        m_sourceToTreeItem.erase(assetToRemove->GetData()->m_assetDbName);
        const AZStd::shared_ptr<const SourceAssetTreeItemData> sourceItemData = AZStd::rtti_pointer_cast<const SourceAssetTreeItemData>(assetToRemove->GetData());
        if (sourceItemData && sourceItemData->m_hasDatabaseInfo)
        {
            m_sourceIdToTreeItem.erase(sourceItemData->m_sourceInfo.m_sourceID);
        }
        parent->EraseChild(assetToRemove);

        endRemoveRows();

        RemoveFoldersIfEmpty(parent);
    }

    void SourceAssetTreeModel::OnSourceFileRemoved(AZ::s64 sourceId)
    {
        // UI changes need to be done on the main thread.
        AZ::SystemTickBus::QueueFunction([&, sourceId]()
        {
            auto existingSource = m_sourceIdToTreeItem.find(sourceId);
            if (existingSource == m_sourceIdToTreeItem.end() || !existingSource->second)
            {
                // If the asset being removed wasn't previously cached, then something has gone wrong. Reset the model.
                Reset();
                return;
            }
            RemoveAssetTreeItem(existingSource->second);
        });
    }

    QModelIndex SourceAssetTreeModel::GetIndexForSource(const AZStd::string& source)
    {
        auto sourceItem = m_sourceToTreeItem.find(source);
        if (sourceItem == m_sourceToTreeItem.end())
        {
            return QModelIndex();
        }
        return createIndex(sourceItem->second->GetRow(), 0, sourceItem->second);
    }
}
