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
#pragma once

#include "AssetTreeModel.h"
#include <AzToolsFramework/API/AssetDatabaseBus.h>
#include <AzCore/std/containers/unordered_map.h>

#include <QDir>

namespace AssetProcessor
{
    class SourceAssetTreeModel : public AssetTreeModel, AzToolsFramework::AssetDatabase::AssetDatabaseNotificationBus::Handler
    {
    public:
        SourceAssetTreeModel(QObject *parent = nullptr);
        ~SourceAssetTreeModel();

        // AssetDatabaseNotificationBus::Handler
        void OnSourceFileChanged(const AzToolsFramework::AssetDatabase::SourceDatabaseEntry& entry) override;
        void OnSourceFileRemoved(AZ::s64 sourceId) override;

        QModelIndex GetIndexForSource(const AZStd::string& source);

    protected:
        void ResetModel() override;

        void AddOrUpdateEntry(
            const AzToolsFramework::AssetDatabase::SourceDatabaseEntry& source,
            const AzToolsFramework::AssetDatabase::ScanFolderDatabaseEntry& scanFolder,
            bool modelIsResetting);

        void RemoveAssetTreeItem(AssetTreeItem* assetToRemove);
        void RemoveFoldersIfEmpty(AssetTreeItem* itemToCheck);

        AZStd::unordered_map<AZStd::string, AssetTreeItem*> m_sourceToTreeItem;
        AZStd::unordered_map<AZ::s64, AssetTreeItem*> m_sourceIdToTreeItem;
        QDir m_assetRoot;
        bool m_assetRootSet = false;
    };
}