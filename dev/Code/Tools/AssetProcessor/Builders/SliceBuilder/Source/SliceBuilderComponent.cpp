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

#include <SliceBuilder/Source/SliceBuilderComponent.h>
#include <AzCore/Slice/SliceComponent.h>
#include <AzCore/Debug/TraceMessageBus.h>
#include <AzCore/Component/ComponentApplication.h>
#include <AzFramework/StringFunc/StringFunc.h>
#include <AzFramework/IO/LocalFileIO.h>
#include <AzToolsFramework/ToolsComponents/EditorComponentBase.h>
#include <AzToolsFramework/ToolsComponents/GenericComponentWrapper.h>
#include <AssetBuilderSDK/AssetBuilderBusses.h>
#include <AzToolsFramework/ToolsComponents/ToolsAssetCatalogBus.h>

#include <AzToolsFramework/Fingerprinting/TypeFingerprinter.h>

namespace SliceBuilder
{
    void BuilderPluginComponent::Activate()
    {
        // Register Slice Builder
        m_sliceBuilder.reset(aznew SliceBuilderWorker);
        AZ::SerializeContext* serializeContext = nullptr;
        AZ::ComponentApplicationBus::BroadcastResult(serializeContext, &AZ::ComponentApplicationBus::Events::GetSerializeContext);
        AZ_Assert(serializeContext, "SerializeContext not found");

        AzToolsFramework::Fingerprinting::TypeCollection types;

        serializeContext->EnumerateDerived(
            [&types]
                (const AZ::SerializeContext::ClassData* classData, const AZ::Uuid& /*knownType*/)
                {
                    if (!classData->m_azRtti->IsAbstract())
                    {
                        types.insert(classData->m_typeId);
                    }
                    return true;
                },
            azrtti_typeid<AZ::Component>(), azrtti_typeid<AZ::Component>());

        AzToolsFramework::Fingerprinting::TypeFingerprinter fingerprinter(*serializeContext);
        AzToolsFramework::Fingerprinting::TypeFingerprint allComponents = fingerprinter.GenerateFingerprintForAllTypes(types);
        AZStd::string builderAnalysisFingerprint = AZStd::string::format("%llu", allComponents);

        // Include slice builder settings in the fingerprint so changes trigger job reruns
        if (!m_sliceBuilder->SliceUpgradesAllowed())
        {
            builderAnalysisFingerprint.append("|UpgradeDisabled");
        }

        AssetBuilderSDK::AssetBuilderDesc builderDescriptor;
        builderDescriptor.m_name = "Slice Builder";
        builderDescriptor.m_version = 5;
        builderDescriptor.m_analysisFingerprint = builderAnalysisFingerprint;
        builderDescriptor.m_patterns.push_back(AssetBuilderSDK::AssetBuilderPattern(AZ::SliceAsset::GetFileFilter(), AssetBuilderSDK::AssetBuilderPattern::PatternType::Wildcard));
        builderDescriptor.m_busId = SliceBuilderWorker::GetUUID();
        builderDescriptor.m_createJobFunction = AZStd::bind(&SliceBuilderWorker::CreateJobs, m_sliceBuilder.get(), AZStd::placeholders::_1, AZStd::placeholders::_2);
        builderDescriptor.m_processJobFunction = AZStd::bind(&SliceBuilderWorker::ProcessJob, m_sliceBuilder.get(), AZStd::placeholders::_1, AZStd::placeholders::_2);

        AssetBuilderSDK::AssetBuilderBus::Broadcast(&AssetBuilderSDK::AssetBuilderBus::Handler::RegisterBuilderInformation, builderDescriptor);

        AzToolsFramework::ToolsAssetSystemBus::Broadcast(&AzToolsFramework::ToolsAssetSystemRequests::RegisterSourceAssetType, azrtti_typeid<AZ::SliceAsset>(), AZ::SliceAsset::GetFileFilter());
    }

    void BuilderPluginComponent::Deactivate()
    {
        // Finish all queued work
        AZ::Data::AssetBus::ExecuteQueuedEvents();

        AzToolsFramework::ToolsAssetSystemBus::Broadcast(&AzToolsFramework::ToolsAssetSystemRequests::UnregisterSourceAssetType, azrtti_typeid<AZ::SliceAsset>());

        m_sliceBuilder.reset();
    }

    void BuilderPluginComponent::Reflect(AZ::ReflectContext* context)
    {
        SliceBuilderSettings::Reflect(context);

        if (AZ::SerializeContext* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<BuilderPluginComponent, AZ::Component>()
                ->Version(1)
                ;
        }
    }
}
