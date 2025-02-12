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

#include "Vegetation_precompiled.h"
#include "DistanceBetweenFilterComponent.h"
#include <AzCore/Component/Entity.h>
#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <Vegetation/Descriptor.h>
#include <GradientSignal/Ebuses/GradientRequestBus.h>
#include <Vegetation/InstanceData.h>
#include <Vegetation/Ebuses/AreaSystemRequestBus.h>
#include <AzCore/Debug/Profiler.h>
#include <LmbrCentral/Dependency/DependencyMonitor.h>

#include <Vegetation/Ebuses/DebugNotificationBus.h>

namespace Vegetation
{
    void DistanceBetweenFilterConfig::Reflect(AZ::ReflectContext* context)
    {
        AZ::SerializeContext* serialize = azrtti_cast<AZ::SerializeContext*>(context);
        if (serialize)
        {
            serialize->Class<DistanceBetweenFilterConfig, AZ::ComponentConfig>()
                ->Version(0)
                ->Field("AllowOverrides", &DistanceBetweenFilterConfig::m_allowOverrides)
                ->Field("BoundMode", &DistanceBetweenFilterConfig::m_boundMode)
                ->Field("RadiusMin", &DistanceBetweenFilterConfig::m_radiusMin)
                ;

            AZ::EditContext* edit = serialize->GetEditContext();
            if (edit)
            {
                edit->Class<DistanceBetweenFilterConfig>(
                    "Vegetation Distance Between Filter", "")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::ShowChildrenOnly)
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::CheckBox, &DistanceBetweenFilterConfig::m_allowOverrides, "Allow Per-Item Overrides", "Allow per-descriptor parameters to override component parameters.")
                    ->DataElement(AZ::Edit::UIHandlers::ComboBox, &DistanceBetweenFilterConfig::m_boundMode, "Bound Mode", "")
                    ->Attribute(AZ::Edit::Attributes::ChangeNotify, AZ::Edit::PropertyRefreshLevels::AttributesAndValues)
                    ->EnumAttribute(BoundMode::Radius, "Radius")
                    ->EnumAttribute(BoundMode::MeshRadius, "MeshRadius")
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &DistanceBetweenFilterConfig::m_radiusMin, "Radius Min", "Minimum test radius between instances for filter to pass.")
                    ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                    ->Attribute(AZ::Edit::Attributes::Max, std::numeric_limits<float>::max())
                    ->Attribute(AZ::Edit::Attributes::SoftMax, 16.0f) // match current default sector size in meters.
                    ->Attribute(AZ::Edit::Attributes::ReadOnly, &DistanceBetweenFilterConfig::IsRadiusReadOnly)
                    ;
            }
        }

        if (auto behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->Class<DistanceBetweenFilterConfig>()
                ->Attribute(AZ::Script::Attributes::ExcludeFrom, AZ::Script::Attributes::ExcludeFlags::Preview)
                ->Attribute(AZ::Script::Attributes::Category, "Vegetation")
                ->Constructor()
                ->Property("allowOverrides", BehaviorValueProperty(&DistanceBetweenFilterConfig::m_allowOverrides))
                ->Property("boundMode",
                    [](DistanceBetweenFilterConfig* config) { return (AZ::u8&)(config->m_boundMode); },
                    [](DistanceBetweenFilterConfig* config, const AZ::u8& i) { config->m_boundMode = (BoundMode)i; })
                ->Property("radiusMin", BehaviorValueProperty(&DistanceBetweenFilterConfig::m_radiusMin))
                ;
        }
    }

    bool DistanceBetweenFilterConfig::IsRadiusReadOnly() const
    {
        return m_boundMode != BoundMode::Radius;
    }

    void DistanceBetweenFilterComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& services)
    {
        services.push_back(AZ_CRC("VegetationFilterService", 0x9f97cc97));
        services.push_back(AZ_CRC("VegetationDistanceBetweenFilterService", 0xd9654a5b));
    }

    void DistanceBetweenFilterComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& services)
    {
        services.push_back(AZ_CRC("VegetationDistanceBetweenFilterService", 0xd9654a5b));
    }

    void DistanceBetweenFilterComponent::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& services)
    {
        services.push_back(AZ_CRC("VegetationAreaService", 0x6a859504));
    }

    void DistanceBetweenFilterComponent::Reflect(AZ::ReflectContext* context)
    {
        DistanceBetweenFilterConfig::Reflect(context);

        AZ::SerializeContext* serialize = azrtti_cast<AZ::SerializeContext*>(context);
        if (serialize)
        {
            serialize->Class<DistanceBetweenFilterComponent, AZ::Component>()
                ->Version(0)
                ->Field("Configuration", &DistanceBetweenFilterComponent::m_configuration)
                ;
        }

        if (auto behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->Constant("DistanceBetweenFilterComponentTypeId", BehaviorConstant(DistanceBetweenFilterComponentTypeId));

            behaviorContext->Class<DistanceBetweenFilterComponent>()->RequestBus("DistanceBetweenFilterRequestBus");

            behaviorContext->EBus<DistanceBetweenFilterRequestBus>("DistanceBetweenFilterRequestBus")
                ->Attribute(AZ::Script::Attributes::ExcludeFrom, AZ::Script::Attributes::ExcludeFlags::Preview)
                ->Attribute(AZ::Script::Attributes::Category, "Vegetation")
                ->Event("GetAllowOverrides", &DistanceBetweenFilterRequestBus::Events::GetAllowOverrides)
                ->Event("SetAllowOverrides", &DistanceBetweenFilterRequestBus::Events::SetAllowOverrides)
                ->VirtualProperty("AllowOverrides", "GetAllowOverrides", "SetAllowOverrides")
                ->Event("GetBoundMode", &DistanceBetweenFilterRequestBus::Events::GetBoundMode)
                ->Event("SetBoundMode", &DistanceBetweenFilterRequestBus::Events::SetBoundMode)
                ->VirtualProperty("BoundMode", "GetBoundMode", "SetBoundMode")
                ->Event("GetRadiusMin", &DistanceBetweenFilterRequestBus::Events::GetRadiusMin)
                ->Event("SetRadiusMin", &DistanceBetweenFilterRequestBus::Events::SetRadiusMin)
                ->VirtualProperty("RadiusMin", "GetRadiusMin", "SetRadiusMin")
                ;
        }
    }

    DistanceBetweenFilterComponent::DistanceBetweenFilterComponent(const DistanceBetweenFilterConfig& configuration)
        : m_configuration(configuration)
    {
    }

    void DistanceBetweenFilterComponent::Activate()
    {
        FilterRequestBus::Handler::BusConnect(GetEntityId());
        DistanceBetweenFilterRequestBus::Handler::BusConnect(GetEntityId());
    }

    void DistanceBetweenFilterComponent::Deactivate()
    {
        FilterRequestBus::Handler::BusDisconnect();
        DistanceBetweenFilterRequestBus::Handler::BusDisconnect();
    }

    bool DistanceBetweenFilterComponent::ReadInConfig(const AZ::ComponentConfig* baseConfig)
    {
        if (auto config = azrtti_cast<const DistanceBetweenFilterConfig*>(baseConfig))
        {
            m_configuration = *config;
            return true;
        }
        return false;
    }

    bool DistanceBetweenFilterComponent::WriteOutConfig(AZ::ComponentConfig* outBaseConfig) const
    {
        if (auto config = azrtti_cast<DistanceBetweenFilterConfig*>(outBaseConfig))
        {
            *config = m_configuration;
            return true;
        }
        return false;
    }

    AZ::Aabb DistanceBetweenFilterComponent::GetInstanceBounds(const InstanceData& instanceData) const
    {
        if (instanceData.m_descriptorPtr)
        {
            if (m_configuration.m_allowOverrides && instanceData.m_descriptorPtr->m_radiusOverrideEnabled)
            {
                return AZ::Aabb::CreateCenterRadius(instanceData.m_position, instanceData.m_descriptorPtr->GetRadius() * instanceData.m_scale);
            }

            if (m_configuration.m_boundMode == BoundMode::MeshRadius)
            {
                return AZ::Aabb::CreateCenterRadius(instanceData.m_position, instanceData.m_descriptorPtr->m_meshRadius * instanceData.m_scale);
            }
        }

        return AZ::Aabb::CreateCenterRadius(instanceData.m_position, m_configuration.m_radiusMin * instanceData.m_scale);
    }

    bool DistanceBetweenFilterComponent::Evaluate(const InstanceData& instanceData) const
    {
        AZ_PROFILE_FUNCTION(AZ::Debug::ProfileCategory::Entity);

        bool intersects = false;

        //only process if configured to have an effect
        if (m_configuration.m_allowOverrides || m_configuration.m_boundMode == BoundMode::MeshRadius || m_configuration.m_radiusMin > 0.0f)
        {
            const AZ::Aabb& instanceAabb = GetInstanceBounds(instanceData);
            AreaSystemRequestBus::Broadcast(&AreaSystemRequestBus::Events::EnumerateInstancesInOverlappingSectors,
                instanceAabb,
                [this, &intersects, &instanceAabb](const InstanceData& neighborData) {
                const AZ::Aabb& neighborAabb = GetInstanceBounds(neighborData);
                if (instanceAabb.Overlaps(neighborAabb))
                {
                    intersects = true;
                    return AreaSystemEnumerateCallbackResult::StopEnumerating;
                }
                return AreaSystemEnumerateCallbackResult::KeepEnumerating;
            });

            if (intersects)
            {
                VEG_PROFILE_METHOD(DebugNotificationBus::QueueBroadcast(&DebugNotificationBus::Events::FilterInstance, instanceData.m_id, AZStd::string_view("DistanceBetweenFilter")));
            }
        }

        return !intersects;
    }

    FilterStage DistanceBetweenFilterComponent::GetFilterStage() const
    {
        return FilterStage::PostProcess;
    }

    void DistanceBetweenFilterComponent::SetFilterStage(FilterStage filterStage)
    {
        // We do nothing here since we're a hard coded filter stage
    }

    bool DistanceBetweenFilterComponent::GetAllowOverrides() const
    {
        return m_configuration.m_allowOverrides;
    }

    void DistanceBetweenFilterComponent::SetAllowOverrides(bool value)
    {
        m_configuration.m_allowOverrides = value;
        LmbrCentral::DependencyNotificationBus::Event(GetEntityId(), &LmbrCentral::DependencyNotificationBus::Events::OnCompositionChanged);
    }

    BoundMode DistanceBetweenFilterComponent::GetBoundMode() const
    {
        return m_configuration.m_boundMode;
    }

    void DistanceBetweenFilterComponent::SetBoundMode(BoundMode boundMode)
    {
        m_configuration.m_boundMode = boundMode;
        LmbrCentral::DependencyNotificationBus::Event(GetEntityId(), &LmbrCentral::DependencyNotificationBus::Events::OnCompositionChanged);
    }

    float DistanceBetweenFilterComponent::GetRadiusMin() const
    {
        return m_configuration.m_radiusMin;
    }

    void DistanceBetweenFilterComponent::SetRadiusMin(float radiusMin)
    {
        m_configuration.m_radiusMin = radiusMin;
        LmbrCentral::DependencyNotificationBus::Event(GetEntityId(), &LmbrCentral::DependencyNotificationBus::Events::OnCompositionChanged);
    }
}
