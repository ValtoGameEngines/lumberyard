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

#include <AzCore/Component/EntityId.h>
#include <AzCore/Asset/AssetCommon.h>

#include <ScriptCanvas/Core/Core.h>

namespace ScriptCanvasEditor
{
    class ScriptCanvasAsset;

    /*! ScriptCanvasAssetHolder
    Wraps a ScriptCanvasAsset reference and registers for the individual AssetBus events
    for saving, loading and unloading the asset.
    The ScriptCanvasAsset Holder contains functionality for activating the ScriptCanvasEntity stored on the reference asset
    as well as attempting to open the ScriptCanvasAsset within the ScriptCanvas Editor.
    It also provides the EditContext reflection for opening the asset in the ScriptCanvas Editor via a button
    */
    class ScriptCanvasAssetHolder
        : private AZ::Data::AssetBus::Handler
    {
    public:
        AZ_RTTI(ScriptCanvasAssetHolder, "{3E80CEE3-2932-4DC1-AADF-398FDDC6DEFE}");
        AZ_CLASS_ALLOCATOR(ScriptCanvasAssetHolder, AZ::SystemAllocator, 0);

        using ScriptChangedCB = AZStd::function<void(const AZ::Data::Asset<ScriptCanvasAsset>&)>;

        ScriptCanvasAssetHolder();
        ScriptCanvasAssetHolder(AZ::Data::Asset<ScriptCanvasAsset> asset, const ScriptChangedCB& = {});
        ~ScriptCanvasAssetHolder() override;
        
        static void Reflect(AZ::ReflectContext* context);
        void Init(AZ::EntityId ownerId = AZ::EntityId());

        void SetAsset(const AZ::Data::Asset<ScriptCanvasAsset>& asset);
        AZ::Data::Asset<ScriptCanvasAsset> GetAsset() const;
        AZ::Data::AssetId GetAssetId() const;

        ScriptCanvas::ScriptCanvasId GetScriptCanvasId() const;

        void LaunchScriptCanvasEditor(const AZ::Data::AssetId&, const AZ::Data::AssetType&) const;
        void OpenEditor() const;

        void SetScriptChangedCB(const ScriptChangedCB&);
        void Load(bool loadBlocking = false);

    protected:

        //=====================================================================
        // AZ::Data::AssetBus
        void OnAssetReady(AZ::Data::Asset<AZ::Data::AssetData> asset) override;
        void OnAssetReloaded(AZ::Data::Asset<AZ::Data::AssetData> asset) override;
        void OnAssetUnloaded(const AZ::Data::AssetId assetId, const AZ::Data::AssetType assetType) override;
        void OnAssetSaved(AZ::Data::Asset<AZ::Data::AssetData> asset, bool isSuccessful) override;
        //=====================================================================

        //! Reloads the Script From the AssetData if it has changed
        AZ::u32 OnScriptChanged();

        AZ::Data::Asset<ScriptCanvasAsset> m_scriptCanvasAsset;

        AZ::EntityId m_ownerId; // Id of Entity which stores this AssetHolder object
        ScriptChangedCB m_scriptNotifyCallback;
    };

}