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
#include "PhysX_precompiled.h"

#include <Source/ForceRegion.h>

#include <PhysX/MeshAsset.h>
#include <PhysX/ColliderShapeBus.h>
#include <Source/Utils.h>

#include <AzCore/Math/Color.h>
#include <AzFramework/Physics/RigidBodyBus.h>

namespace PhysX
{
    /// Aggregates the AABB of all trigger collider components in an entity.
    struct TriggerAabbAggregator
    {
        AZ::Aabb operator()(AZ::Aabb& lhs, const AZ::Aabb& rhs) const
        {
            if (rhs == AZ::Aabb::CreateNull()) // Ignore non-trigger colliders that may have null AABB.
            {
                return lhs;
            }
            else
            {
                lhs.AddAabb(rhs);
                return lhs;
            }
        }
    };

    /// Aggregates points on trigger collider components in an entity.
    struct TriggerRandomPointsAggregator
    {
        Utils::Geometry::PointList operator()(Utils::Geometry::PointList& left
            , const Utils::Geometry::PointList& right) const
        {
            Utils::Geometry::PointList combinedPoints;
            combinedPoints.reserve(left.size() + right.size());
            combinedPoints.insert(combinedPoints.end(), left.begin(), left.end());
            combinedPoints.insert(combinedPoints.end(), right.begin(), right.end());
            return combinedPoints;
        }
    };

    void ForceRegion::Reflect(AZ::ReflectContext* context)
    {
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            BaseForce::Reflect(*serializeContext);

            serializeContext->Class<ForceRegion>()
                ->Version(1)
                ->Field("Forces", &ForceRegion::m_forces)
                ;

            if (auto editContext = serializeContext->GetEditContext())
            {
                editContext->Class<ForceRegion>(
                    "Force Region", "Applies forces on entities within a region")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ForceRegion::m_forces, "Forces", "Forces acting in the region")
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ;
            }
        }
    }

    ForceRegion::ForceRegion(const ForceRegion& forceRegion)
    {
        // Force region must be deep copied as it contains pointers
        AZ::SerializeContext* context;
        AZ::ComponentApplicationBus::BroadcastResult(context, &AZ::ComponentApplicationBus::Events::GetSerializeContext);
        context->CloneObjectInplace<ForceRegion>(*this, &forceRegion);
    }

    void ForceRegion::Activate(AZ::EntityId entityId)
    {
        m_entityId = entityId;
        m_regionParams = ForceRegionUtil::CreateRegionParams(m_entityId);
        AZ::TransformNotificationBus::MultiHandler::BusConnect(m_entityId);
        LmbrCentral::SplineComponentNotificationBus::Handler::BusConnect(m_entityId);
        ForceRegionRequestBus::Handler::BusConnect(m_entityId);
        PhysX::ColliderComponentEventBus::Handler::BusConnect(m_entityId);
        for (auto& force : m_forces)
        {
            force->Activate(m_entityId);
        }

        AZ::TransformBus::EventResult(m_worldTransform, m_entityId, &AZ::TransformBus::Events::GetWorldTM);
    }

    void ForceRegion::Deactivate()
    {
        m_entityId.SetInvalid();
        for (auto& force : m_forces)
        {
            force->Deactivate();
        }
        PhysX::ColliderComponentEventBus::Handler::BusDisconnect();
        ForceRegionRequestBus::Handler::BusDisconnect();
        LmbrCentral::SplineComponentNotificationBus::Handler::BusDisconnect();
        AZ::TransformNotificationBus::MultiHandler::BusDisconnect();
    }

    AZ::Vector3 ForceRegion::CalculateNetForce(const EntityParams& entity) const
    {
        auto totalForce = AZ::Vector3::CreateZero();
        for (auto& force : m_forces)
        {
            totalForce += force->CalculateForce(entity, m_regionParams);
        }
        PhysX::ForceRegionNotificationBus::Broadcast(&ForceRegionNotificationBus::Events::OnCalculateNetForce
            , m_regionParams.m_id
            , entity.m_id
            , totalForce.GetNormalized()
            , totalForce.GetLength());
        return totalForce;
    }

    void ForceRegion::ClearForces()
    {
        for (auto& force : m_forces)
        {
            force->Deactivate();
        }
        m_forces.clear();
    }

    PhysX::RegionParams ForceRegion::GetRegionParams() const
    {
        return m_regionParams;
    }

    void ForceRegion::OnTransformChanged(const AZ::Transform& /*local*/, const AZ::Transform& world)
    {
        m_worldTransform = world;
        m_regionParams.m_position = world.GetPosition();
        AZ::Transform rotate = world;
        m_regionParams.m_scale = rotate.ExtractScaleExact();
        m_regionParams.m_rotation = AZ::Quaternion::CreateFromTransform(rotate);
        AZ::EBusReduceResult<AZ::Aabb, PhysX::TriggerAabbAggregator> triggerAabb;
        triggerAabb.value = AZ::Aabb::CreateNull();

        ColliderShapeRequestBus::EventResult(triggerAabb
            , m_entityId
            , &ColliderShapeRequestBus::Events::GetColliderShapeAabb);

        m_regionParams.m_aabb = triggerAabb.value;
    }

    void ForceRegion::OnColliderChanged()
    {
        m_regionParams = ForceRegionUtil::CreateRegionParams(m_entityId);
    }

    void ForceRegion::AddForceWorldSpace(const AZ::Vector3& direction, float magnitude)
    {
        AddAndActivateForce(AZStd::make_unique<ForceWorldSpace>(direction, magnitude));
    }

    void ForceRegion::AddForceLocalSpace(const AZ::Vector3& direction, float magnitude)
    {
        AddAndActivateForce(AZStd::make_unique<ForceLocalSpace>(direction, magnitude));
    }

    void ForceRegion::AddForcePoint(float magnitude)
    {
        AddAndActivateForce(AZStd::make_unique<ForcePoint>(magnitude));
    }

    void ForceRegion::AddForceSplineFollow(float dampingRatio, float frequency, float targetSpeed, float lookAhead)
    {
        AddAndActivateForce(AZStd::make_unique<ForceSplineFollow>(dampingRatio, frequency, targetSpeed, lookAhead));
    }

    void ForceRegion::AddForceSimpleDrag(float dragCoefficient, float volumeDensity)
    {
        AddAndActivateForce(AZStd::make_unique<ForceSimpleDrag>(dragCoefficient, volumeDensity));
    }

    void ForceRegion::AddForceLinearDamping(float damping)
    {
        AddAndActivateForce(AZStd::make_unique<ForceLinearDamping>(damping));
    }

    void ForceRegion::AddAndActivateForce(AZStd::unique_ptr<BaseForce> force)
    {
        AZ_Assert(force, "Failed to add and activate null force.");
        if (nullptr == force)
        {
            return;
        }
        m_forces.push_back(AZStd::move(force));
        m_forces.back()->Activate(m_entityId);
    }

    void ForceRegion::OnSplineChanged()
    {
        LmbrCentral::SplineComponentRequestBus::EventResult(m_regionParams.m_spline
            , m_entityId
            , &LmbrCentral::SplineComponentRequestBus::Events::GetSpline);
    }

    RegionParams ForceRegionUtil::CreateRegionParams(const AZ::EntityId& entityId)
    {
        RegionParams regionParams;
        regionParams.m_id = entityId;

        AZ::Transform worldTransform = AZ::Transform::CreateIdentity();
        AZ::TransformBus::EventResult(worldTransform
            , entityId
            , &AZ::TransformBus::Events::GetWorldTM);
        regionParams.m_position = worldTransform.GetPosition();
        regionParams.m_scale = worldTransform.ExtractScaleExact();
        regionParams.m_rotation = AZ::Quaternion::CreateFromTransform(worldTransform);

        LmbrCentral::SplineComponentRequestBus::EventResult(regionParams.m_spline
            , entityId
            , &LmbrCentral::SplineComponentRequestBus::Events::GetSpline);
        AZ::EBusReduceResult<AZ::Aabb, PhysX::TriggerAabbAggregator> triggerAabb;
        triggerAabb.value = AZ::Aabb::CreateNull();
        ColliderShapeRequestBus::EventResult(triggerAabb
            , entityId
            , &ColliderShapeRequestBus::Events::GetColliderShapeAabb);
        regionParams.m_aabb = triggerAabb.value;
        return regionParams;
    }

    EntityParams ForceRegionUtil::CreateEntityParams(const AZ::EntityId& entityId)
    {
        EntityParams entityParams;
        entityParams.m_id = entityId;
        AZ::TransformBus::EventResult(entityParams.m_position
            , entityId
            , &AZ::TransformBus::Events::GetWorldTranslation);
        Physics::RigidBodyRequestBus::EventResultReverse(entityParams.m_velocity
            , entityId
            , &Physics::RigidBodyRequestBus::Events::GetLinearVelocity);
        Physics::RigidBodyRequestBus::EventResultReverse(entityParams.m_mass
            , entityId
            , &Physics::RigidBodyRequestBus::Events::GetMass);
        AZ::EBusReduceResult<AZ::Aabb, PhysX::TriggerAabbAggregator> triggerAabb;
        triggerAabb.value = AZ::Aabb::CreateNull();
        ColliderShapeRequestBus::EventResult(triggerAabb
            , entityId
            , &ColliderShapeRequestBus::Events::GetColliderShapeAabb);
        entityParams.m_aabb = triggerAabb.value;
        return entityParams;
    }
}
