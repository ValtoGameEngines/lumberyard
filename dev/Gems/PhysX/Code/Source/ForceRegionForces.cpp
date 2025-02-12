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
#include "ForceRegionForces.h"

namespace PhysX
{
    static const float s_forceRegionZeroValue = 0.0f;
    static const float s_forceRegionMaxDamping = 100.0f; // Large values create an oscillation that sends the body too far out. Legacy renderer's Octree may throw errors.
    static const float s_forceRegionMaxDensity = 400.0f; // Large values create an oscillation that sends the body too far out. Legacy renderer's Octree may throw errors. Maximum density is defined as a value capable of slowing down a radius 1 ball weighing 1 ton.
    static const float s_forceRegionMaxValue = 1000000.0f;
    static const float s_forceRegionMinValue = -s_forceRegionMaxValue;
    static const float s_forceRegionMaxDampingRatio = 1.5f;
    static const float s_forceRegionMinFrequency = 0.1f;
    static const float s_forceRegionMaxFrequency = 10.0f;



    ForceWorldSpace::ForceWorldSpace(const AZ::Vector3& direction, const float magnitude)
        : m_direction(direction)
        , m_magnitude(magnitude)
    {
    }

    void ForceWorldSpace::Reflect(AZ::ReflectContext* context)
    {
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<ForceWorldSpace, BaseForce>()
                ->Field("Direction", &ForceWorldSpace::m_direction)
                ->Field("Magnitude", &ForceWorldSpace::m_magnitude)
                ;

            if (auto editContext = serializeContext->GetEditContext())
            {
                editContext->Class<ForceWorldSpace>(
                    "World Space Force", "Applies a force in world space")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Vector3, &ForceWorldSpace::m_direction, "Direction", "Direction of the force in world space")
                      ->Attribute(AZ::Edit::Attributes::Min, s_forceRegionMinValue)
                      ->Attribute(AZ::Edit::Attributes::Max, s_forceRegionMaxValue)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ForceWorldSpace::m_magnitude, "Magnitude", "Magnitude of the force in world space")
                      ->Attribute(AZ::Edit::Attributes::Min, s_forceRegionMinValue)
                      ->Attribute(AZ::Edit::Attributes::Max, s_forceRegionMaxValue)
                    ;
            }
        }

        if (auto behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->EBus<ForceWorldSpaceRequestBus>("ForceWorldSpaceRequestBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Module, "physics")
                ->Attribute(AZ::Script::Attributes::ExcludeFrom, AZ::Script::Attributes::Preview)
                ->Attribute(AZ::Script::Attributes::Category, "PhysX")
                ->Event("SetDirection", &ForceWorldSpaceRequestBus::Events::SetDirection)
                ->Event("GetDirection", &ForceWorldSpaceRequestBus::Events::GetDirection)
                ->Event("SetMagnitude", &ForceWorldSpaceRequestBus::Events::SetMagnitude)
                ->Event("GetMagnitude", &ForceWorldSpaceRequestBus::Events::GetMagnitude)
                ;
        }
    }

    AZ::Vector3 ForceWorldSpace::CalculateForce(const EntityParams& entity, const RegionParams& region) const
    {
        return m_direction.GetNormalized() * m_magnitude * entity.m_mass;
    }

    ForceLocalSpace::ForceLocalSpace(const AZ::Vector3& direction, const float magnitude) :
        m_direction(direction)
        , m_magnitude(magnitude)
    {
    }

    void ForceLocalSpace::Reflect(AZ::ReflectContext* context)
    {
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<ForceLocalSpace, BaseForce>()
                ->Field("Direction", &ForceLocalSpace::m_direction)
                ->Field("Magnitude", &ForceLocalSpace::m_magnitude)
                ;

            if (auto editContext = serializeContext->GetEditContext())
            {
                editContext->Class<ForceLocalSpace>(
                    "Local Space Force", "Applies a force in the volume's local space")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Vector3, &ForceLocalSpace::m_direction, "Direction", "Direction of the force in local space")
                      ->Attribute(AZ::Edit::Attributes::Min, s_forceRegionMinValue)
                      ->Attribute(AZ::Edit::Attributes::Max, s_forceRegionMaxValue)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ForceLocalSpace::m_magnitude, "Magnitude", "Magnitude of the force in local space")
                      ->Attribute(AZ::Edit::Attributes::Min, s_forceRegionMinValue)
                      ->Attribute(AZ::Edit::Attributes::Max, s_forceRegionMaxValue)
                    ;
            }
        }

        if (auto behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->EBus<ForceLocalSpaceRequestBus>("ForceLocalSpaceRequestBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Module, "physics")
                ->Attribute(AZ::Script::Attributes::ExcludeFrom, AZ::Script::Attributes::Preview)
                ->Attribute(AZ::Script::Attributes::Category, "PhysX")
                ->Event("SetDirection", &ForceLocalSpaceRequestBus::Events::SetDirection)
                ->Event("GetDirection", &ForceLocalSpaceRequestBus::Events::GetDirection)
                ->Event("SetMagnitude", &ForceLocalSpaceRequestBus::Events::SetMagnitude)
                ->Event("GetMagnitude", &ForceLocalSpaceRequestBus::Events::GetMagnitude)
                ;
        }
    }

    AZ::Vector3 ForceLocalSpace::CalculateForce(const EntityParams& entity, const RegionParams& region) const
    {
        return region.m_rotation * m_direction.GetNormalized() * m_magnitude * entity.m_mass;
    }

    ForcePoint::ForcePoint(float magnitude) 
        : m_magnitude(magnitude)
    {
    }

    void ForcePoint::Reflect(AZ::ReflectContext* context)
    {
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<ForcePoint, BaseForce>()
                ->Field("Magnitude", &ForcePoint::m_magnitude)
                ;

            if (auto editContext = serializeContext->GetEditContext())
            {
                editContext->Class<ForcePoint>(
                    "Point Force", "Applies a force relative to the center of the volume")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ForcePoint::m_magnitude, "Magnitude", "Magnitude of the point force")
                      ->Attribute(AZ::Edit::Attributes::Min, s_forceRegionMinValue)
                      ->Attribute(AZ::Edit::Attributes::Max, s_forceRegionMaxValue)
                    ;
            }
        }

        if (auto behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->EBus<ForcePointRequestBus>("ForcePointRequestBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Module, "physics")
                ->Attribute(AZ::Script::Attributes::ExcludeFrom, AZ::Script::Attributes::Preview)
                ->Attribute(AZ::Script::Attributes::Category, "PhysX")
                ->Event("SetMagnitude", &ForcePointRequestBus::Events::SetMagnitude)
                ->Event("GetMagnitude", &ForcePointRequestBus::Events::GetMagnitude)
                ;
        }
    }

    AZ::Vector3 ForcePoint::CalculateForce(const EntityParams& entity, const RegionParams& region) const
    {
        return (entity.m_position - region.m_aabb.GetCenter()).GetNormalizedSafe() * m_magnitude;
    }

    ForceSplineFollow::ForceSplineFollow(float dampingRatio
        , float frequency
        , float targetSpeed
        , float lookAhead) :
        m_dampingRatio(dampingRatio)
        , m_frequency(frequency)
        , m_targetSpeed(targetSpeed)
        , m_lookAhead(lookAhead)
    {
    }

    void ForceSplineFollow::Reflect(AZ::ReflectContext* context)
    {
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<ForceSplineFollow, BaseForce>()
                ->Field("DampingRatio", &ForceSplineFollow::m_dampingRatio)
                ->Field("Frequency", &ForceSplineFollow::m_frequency)
                ->Field("TargetSpeed", &ForceSplineFollow::m_targetSpeed)
                ->Field("Lookahead", &ForceSplineFollow::m_lookAhead)
                ;

            if (auto editContext = serializeContext->GetEditContext())
            {
                editContext->Class<ForceSplineFollow>(
                    "Spline Follow Force", "Applies a force to make objects follow a spline at a given speed")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ForceSplineFollow::m_dampingRatio, "Damping Ratio", "Amount of damping applied to an entity that is moving towards a spline")
                      ->Attribute(AZ::Edit::Attributes::Min, s_forceRegionZeroValue)
                      ->Attribute(AZ::Edit::Attributes::Max, s_forceRegionMaxDampingRatio)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ForceSplineFollow::m_frequency, "Frequency", "Frequency at which an entity moves towards a spline")
                      ->Attribute(AZ::Edit::Attributes::Min, s_forceRegionMinFrequency)
                      ->Attribute(AZ::Edit::Attributes::Max, s_forceRegionMaxFrequency)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ForceSplineFollow::m_targetSpeed, "Target Speed", "Speed at which entities in the force region move along a spline")
                      ->Attribute(AZ::Edit::Attributes::Min, s_forceRegionMinValue)
                      ->Attribute(AZ::Edit::Attributes::Max, s_forceRegionMaxValue)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ForceSplineFollow::m_lookAhead, "Lookahead", "Distance at which entities look ahead in their path to reach a point on a spline")
                      ->Attribute(AZ::Edit::Attributes::Min, s_forceRegionZeroValue)
                      ->Attribute(AZ::Edit::Attributes::Max, s_forceRegionMaxValue)
                    ;
            }
        }

        if (auto behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->EBus<ForceSplineFollowRequestBus>("ForceSplineFollowRequestBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Module, "physics")
                ->Attribute(AZ::Script::Attributes::ExcludeFrom, AZ::Script::Attributes::Preview)
                ->Attribute(AZ::Script::Attributes::Category, "PhysX")
                ->Event("SetDampingRatio", &ForceSplineFollowRequestBus::Events::SetDampingRatio)
                ->Event("GetDampingRatio", &ForceSplineFollowRequestBus::Events::GetDampingRatio)
                ->Event("SetFrequency", &ForceSplineFollowRequestBus::Events::SetFrequency)
                ->Event("GetFrequency", &ForceSplineFollowRequestBus::Events::GetFrequency)
                ->Event("SetTargetSpeed", &ForceSplineFollowRequestBus::Events::SetTargetSpeed)
                ->Event("GetTargetSpeed", &ForceSplineFollowRequestBus::Events::GetTargetSpeed)
                ->Event("SetLookAhead", &ForceSplineFollowRequestBus::Events::SetLookAhead)
                ->Event("GetLookAhead", &ForceSplineFollowRequestBus::Events::GetLookAhead)
                ;
        }
    }

    AZ::Vector3 ForceSplineFollow::CalculateForce(const EntityParams& entity, const RegionParams& region) const
    {
        if (region.m_spline)
        {
            AZ::Quaternion rotateInverse = region.m_rotation;
            if (!rotateInverse.IsIdentity())
            {
                rotateInverse.InvertFull();
            }

            AZ::Vector3 scaleInverse = region.m_scale;
            scaleInverse = scaleInverse.GetReciprocal();

            AZ::Vector3 position = entity.m_position + entity.m_velocity * m_lookAhead;
            AZ::Vector3 localPos = position - region.m_position;
            localPos = rotateInverse * localPos;
            localPos = localPos * scaleInverse;

            AZ::SplineAddress address = region.m_spline->GetNearestAddressPosition(localPos).m_splineAddress;
            AZ::Vector3 splinePosition = region.m_spline->GetPosition(address);
            AZ::Vector3 splineTangent = region.m_spline->GetTangent(address);

            splinePosition = region.m_scale * splinePosition;
            splineTangent = region.m_scale * splineTangent;

            splinePosition = region.m_rotation * splinePosition;
            splineTangent = region.m_rotation * splineTangent;

            // http://www.matthewpeterkelly.com/tutorials/pdControl/index.html
            float kp = pow((2.0f * AZ::Constants::Pi * m_frequency), 2);
            float kd = 2.0f * m_dampingRatio * (2.0f * AZ::Constants::Pi * m_frequency);

            AZ::Vector3 targetVelocity = splineTangent * m_targetSpeed;
            AZ::Vector3 currentVelocity = entity.m_velocity;

            AZ::Vector3 targetPosition = splinePosition + region.m_position;
            AZ::Vector3 currentPosition = entity.m_position;

            return kp * (targetPosition - currentPosition) + kd * (targetVelocity - currentVelocity);
        }
        else
        {
            return AZ::Vector3::CreateZero();
        }
    }

    void ForceSplineFollow::Activate(AZ::EntityId entityId)
    {
        BusConnect(entityId);
        ForceSplineFollowRequestBus::Handler::BusConnect(entityId);
        m_loggedMissingSplineWarning = false;
    }

    ForceSimpleDrag::ForceSimpleDrag(float dragCoefficient, float volumeDensity) 
        : m_dragCoefficient(dragCoefficient)
        , m_volumeDensity(volumeDensity)
    {
    }

    void ForceSimpleDrag::Reflect(AZ::ReflectContext* context)
    {
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<ForceSimpleDrag, BaseForce>()
                ->Field("Drag Coefficient", &ForceSimpleDrag::m_dragCoefficient)
                ->Field("Volume Density", &ForceSimpleDrag::m_volumeDensity)
                ;

            if (auto editContext = serializeContext->GetEditContext())
            {
                editContext->Class<ForceSimpleDrag>(
                    "Simple Drag Force", "Simulates a drag force on entities")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ForceSimpleDrag::m_volumeDensity, "Region Density", "Density of the region")
                      ->Attribute(AZ::Edit::Attributes::Min, s_forceRegionZeroValue)
                      ->Attribute(AZ::Edit::Attributes::Max, s_forceRegionMaxDensity)
                    ;
            }
        }

        if (auto behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->EBus<ForceSimpleDragRequestBus>("ForceSimpleDragRequestBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Module, "physics")
                ->Attribute(AZ::Script::Attributes::ExcludeFrom, AZ::Script::Attributes::Preview)
                ->Attribute(AZ::Script::Attributes::Category, "PhysX")
                ->Event("SetDensity", &ForceSimpleDragRequestBus::Events::SetDensity)
                ->Event("GetDensity", &ForceSimpleDragRequestBus::Events::GetDensity)
                ;
        }
    }

    AZ::Vector3 ForceSimpleDrag::CalculateForce(const EntityParams& entity, const RegionParams& region) const
    {
        // Aproximate shape as a sphere
        AZ::Vector3 center;
        AZ::VectorFloat radius;
        entity.m_aabb.GetAsSphere(center, radius);

        const AZ::VectorFloat crossSectionalArea = AZ::Constants::Pi * radius * radius;
        const AZ::Vector3 velocitySquared = entity.m_velocity * entity.m_velocity;

        // Wikipedia: https://en.wikipedia.org/wiki/Drag_coefficient
        // Fd = 1/2 * p * u^2 * cd * A
        const AZ::Vector3 dragForce = 0.5f * m_volumeDensity * velocitySquared * m_dragCoefficient * crossSectionalArea;

        // The drag force is defined as being in the same direction as the flow velocity. Since the entity is moving
        // and the volume flow is stationary, this just becomes opposite to the entity's velocity. Causing the object to slow
        // down.
        const AZ::Vector3 direction(-entity.m_velocity.GetX().GetSign(), -entity.m_velocity.GetY().GetSign(), -entity.m_velocity.GetZ().GetSign());

        return dragForce * direction.GetNormalized();
    }

    ForceLinearDamping::ForceLinearDamping(float damping) 
        : m_damping(damping)
    {
    }

    void ForceLinearDamping::Reflect(AZ::ReflectContext* context)
    {
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<ForceLinearDamping, BaseForce>()
                ->Field("Damping", &ForceLinearDamping::m_damping)
                ;

            if (auto editContext = serializeContext->GetEditContext())
            {
                editContext->Class<ForceLinearDamping>(
                    "Linear Damping Force", "Applies an opposite force to the entity's velocity")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ForceLinearDamping::m_damping, "Damping", "Amount of damping applied to an opposite force")
                      ->Attribute(AZ::Edit::Attributes::Min, s_forceRegionZeroValue)
                      ->Attribute(AZ::Edit::Attributes::Max, s_forceRegionMaxDamping)
                    ;
            }
        }

        if (auto behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->EBus<ForceLinearDampingRequestBus>("ForceLinearDampingRequestBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Attribute(AZ::Script::Attributes::Module, "physics")
                ->Attribute(AZ::Script::Attributes::ExcludeFrom, AZ::Script::Attributes::Preview)
                ->Attribute(AZ::Script::Attributes::Category, "PhysX")
                ->Event("SetDamping", &ForceLinearDampingRequestBus::Events::SetDamping)
                ->Event("GetDamping", &ForceLinearDampingRequestBus::Events::GetDamping)
                ;
        }
    }

    AZ::Vector3 ForceLinearDamping::CalculateForce(const EntityParams& entity, const RegionParams& region) const
    {
        return entity.m_velocity * -m_damping * entity.m_mass;
    }

    void BaseForce::Reflect(AZ::SerializeContext& context)
    {
        context.Class<BaseForce>();
    }

    AZ::Vector3 BaseForce::CalculateForce(const EntityParams& /*entityParams*/, const RegionParams& /*volumeParams*/) const
    {
        return AZ::Vector3::CreateZero();
    }
}
