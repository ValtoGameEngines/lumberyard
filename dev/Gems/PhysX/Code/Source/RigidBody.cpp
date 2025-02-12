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

#include <PhysX_precompiled.h>

#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/smart_ptr/shared_ptr.h>
#include <AzFramework/Physics/Utils.h>
#include <PhysX/NativeTypeIdentifiers.h>
#include <PhysX/MathConversion.h>
#include <Source/RigidBody.h>
#include <Source/Utils.h>
#include <PhysX/Utils.h>
#include <Source/Shape.h>
#include <extensions/PxRigidBodyExt.h>
#include <PxPhysicsAPI.h>
#include <AzFramework/Physics/World.h>
#include <PhysX/PhysXLocks.h>

namespace PhysX
{
    void RigidBody::Reflect(AZ::ReflectContext* context)
    {
        AZ::SerializeContext* serializeContext = azrtti_cast<AZ::SerializeContext*>(context);
        if (serializeContext)
        {
            serializeContext->Class<RigidBody>()
                ->Version(1)
            ;
        }
    }

    RigidBody::RigidBody(const Physics::RigidBodyConfiguration& configuration)
        : Physics::RigidBody(configuration)
        , m_startAsleep(configuration.m_startAsleep)
    {
        CreatePhysXActor(configuration);
    }

    RigidBody::~RigidBody()
    {
        //clean up the attached shapes
        {
            PHYSX_SCENE_WRITE_LOCK(m_pxRigidActor->getScene());
            for (auto shape : m_shapes)
            {
                m_pxRigidActor->detachShape(*shape->GetPxShape());
                shape->DetachedFromActor();
            }
        }
        m_shapes.clear();
    }

    void RigidBody::CreatePhysXActor(const Physics::RigidBodyConfiguration& configuration)
    {
        if (m_pxRigidActor != nullptr)
        {
            AZ_Warning("PhysX Rigid Body", false, "Trying to create PhysX rigid actor when it's already created");
            return;
        }

        if (auto rigidBody = PxActorFactories::CreatePxRigidBody(configuration))
        {
            m_pxRigidActor = AZStd::shared_ptr<physx::PxRigidDynamic>(rigidBody, [](auto& actor)
            {
                PxActorFactories::ReleaseActor(actor);
            });

            m_actorUserData = ActorData(m_pxRigidActor.get());
            m_actorUserData.SetRigidBody(this);
            m_actorUserData.SetEntityId(configuration.m_entityId);

            SetName(configuration.m_debugName);
            SetGravityEnabled(configuration.m_gravityEnabled);
            SetSimulationEnabled(configuration.m_simulated);
            SetCCDEnabled(configuration.m_ccdEnabled);

            UpdateCenterOfMassAndInertia(configuration.m_computeCenterOfMass, configuration.m_centerOfMassOffset,
                configuration.m_computeInertiaTensor, configuration.m_inertiaTensor);

            if (configuration.m_customUserData)
            {
                SetUserData(configuration.m_customUserData);
            }
        }
    }

    void RigidBody::AddShape(AZStd::shared_ptr<Physics::Shape> shape)
    {
        if (!m_pxRigidActor || !shape)
        {
            return;
        }

        auto pxShape = AZStd::rtti_pointer_cast<PhysX::Shape>(shape);

        if (!pxShape)
        {
            AZ_Error("PhysX Rigid Body", false, "Trying to add a shape of unknown type. Name: %s", GetName().c_str());
            return;
        }

        if (!pxShape->GetPxShape())
        {
            AZ_Error("PhysX Rigid Body", false, "Trying to add a shape with no valid PxShape. Name: %s", GetName().c_str());
            return;
        }

        if (pxShape->GetPxShape()->getGeometryType() == physx::PxGeometryType::eTRIANGLEMESH)
        {
            AZ_Error("PhysX", false, "Cannot use triangle mesh geometry on a dynamic object: %s", GetName().c_str());
            return;
        }

        {
            PHYSX_SCENE_WRITE_LOCK(m_pxRigidActor->getScene());
            m_pxRigidActor->attachShape(*pxShape->GetPxShape());
        }
        pxShape->AttachedToActor(m_pxRigidActor.get());
        m_shapes.push_back(pxShape);
    }

    void RigidBody::RemoveShape(AZStd::shared_ptr<Physics::Shape> shape)
    {
        if (m_pxRigidActor == nullptr)
        {
            AZ_Warning("PhysX::RigidBody", false, "Trying to remove shape from rigid body with no actor");
            return;
        }

        auto pxShape = AZStd::rtti_pointer_cast<PhysX::Shape>(shape);
        if (!pxShape)
        {
            AZ_Warning("PhysX::RigidBody", false, "Trying to remove shape of unknown type", GetName().c_str());
            return;
        }

        const auto found = AZStd::find(m_shapes.begin(), m_shapes.end(), shape);
        if (found == m_shapes.end())
        {
            AZ_Warning("PhysX::RigidBody", false, "Shape has not been attached to this rigid body: %s", GetName().c_str());
            return;
        }

        {
            PHYSX_SCENE_WRITE_LOCK(m_pxRigidActor->getScene());
            m_pxRigidActor->detachShape(*pxShape->GetPxShape());
        }
        pxShape->DetachedFromActor();
        m_shapes.erase(found);
    }

    void RigidBody::UpdateCenterOfMassAndInertia(bool computeCenterOfMass, const AZ::Vector3& centerOfMassOffset,
        bool computeInertia, const AZ::Matrix3x3& inertiaTensor)
    {
        if (computeCenterOfMass)
        {
            UpdateComputedCenterOfMass();
        }
        else
        {
            SetCenterOfMassOffset(centerOfMassOffset);
        }

        if (computeInertia)
        {
            ComputeInertia();
        }
        else
        {
            SetInertia(inertiaTensor);
        }
    }


    void RigidBody::ReleasePhysXActor()
    {
        m_shapes.clear();
        m_pxRigidActor = nullptr;
    }

    AZ::u32 RigidBody::GetShapeCount()
    {
        return static_cast<AZ::u32>(m_shapes.size());
    }

    AZStd::shared_ptr<Physics::Shape> RigidBody::GetShape(AZ::u32 index)
    {
        if (index >= m_shapes.size())
        {
            return nullptr;
        }
        return m_shapes[index];
    }

    AZ::Vector3 RigidBody::GetCenterOfMassWorld() const
    {
        return m_pxRigidActor ? GetTransform() * GetCenterOfMassLocal() : AZ::Vector3::CreateZero();
    }

    AZ::Vector3 RigidBody::GetCenterOfMassLocal() const
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_READ_LOCK(m_pxRigidActor->getScene());
            return PxMathConvert(m_pxRigidActor->getCMassLocalPose().p);
        }
        return AZ::Vector3::CreateZero();
    }

    AZ::Matrix3x3 RigidBody::GetInverseInertiaWorld() const
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_READ_LOCK(m_pxRigidActor->getScene());
            AZ::Vector3 inverseInertiaDiagonal = PxMathConvert(m_pxRigidActor->getMassSpaceInvInertiaTensor());
            AZ::Matrix3x3 rotationToWorld = AZ::Matrix3x3::CreateFromQuaternion(PxMathConvert(m_pxRigidActor->getGlobalPose().q.getConjugate()));
            return Physics::Utils::InverseInertiaLocalToWorld(inverseInertiaDiagonal, rotationToWorld);
        }

        return AZ::Matrix3x3::CreateZero();
    }

    AZ::Matrix3x3 RigidBody::GetInverseInertiaLocal() const
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_READ_LOCK(m_pxRigidActor->getScene());
            physx::PxVec3 inverseInertiaDiagonal = m_pxRigidActor->getMassSpaceInvInertiaTensor();
            return AZ::Matrix3x3::CreateDiagonal(PxMathConvert(inverseInertiaDiagonal));
        }
        return AZ::Matrix3x3::CreateZero();
    }

    float RigidBody::GetMass() const
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_READ_LOCK(m_pxRigidActor->getScene());
            return m_pxRigidActor->getMass();
        }
        return 0.0f;
    }

    float RigidBody::GetInverseMass() const
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_READ_LOCK(m_pxRigidActor->getScene());
            return m_pxRigidActor->getInvMass();
        }
        return 0.0f;
    }

    void RigidBody::SetMass(float mass)
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_WRITE_LOCK(m_pxRigidActor->getScene());
            m_pxRigidActor->setMass(mass);
        }
    }

    void RigidBody::SetCenterOfMassOffset(const AZ::Vector3& comOffset)
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_WRITE_LOCK(m_pxRigidActor->getScene());
            m_pxRigidActor->setCMassLocalPose(physx::PxTransform(PxMathConvert(comOffset)));
        }
    }

    void RigidBody::UpdateComputedCenterOfMass()
    {
        if (m_pxRigidActor)
        {
            physx::PxU32 shapeCount = 0;
            {
                PHYSX_SCENE_READ_LOCK(m_pxRigidActor->getScene());
                shapeCount = m_pxRigidActor->getNbShapes();
            }
            if (shapeCount > 0)
            {
                AZStd::vector<physx::PxShape*> shapes;
                shapes.resize(shapeCount);

                {
                    PHYSX_SCENE_READ_LOCK(m_pxRigidActor->getScene());
                    m_pxRigidActor->getShapes(&shapes[0], shapeCount);
                }

                shapes.erase(AZStd::remove_if(shapes.begin()
                    , shapes.end()
                    , [](const physx::PxShape* shape)
                      { 
                        return shape->getFlags() & physx::PxShapeFlag::eTRIGGER_SHAPE;
                      })
                    , shapes.end());
                shapeCount = static_cast<physx::PxU32>(shapes.size());

                if (shapeCount == 0)
                {
                    SetZeroCenterOfMass();
                    return;
                }

                const auto properties = physx::PxRigidBodyExt::computeMassPropertiesFromShapes(&shapes[0], shapeCount);
                const physx::PxTransform computedCenterOfMass(properties.centerOfMass);
                {
                    PHYSX_SCENE_WRITE_LOCK(m_pxRigidActor->getScene());
                    m_pxRigidActor->setCMassLocalPose(computedCenterOfMass);
                }
            }
            else
            {
                SetZeroCenterOfMass();
            }
        }
    }

    void RigidBody::SetInertia(const AZ::Matrix3x3& inertia)
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_WRITE_LOCK(m_pxRigidActor->getScene());
            m_pxRigidActor->setMassSpaceInertiaTensor(PxMathConvert(inertia.RetrieveScale()));
        }
    }

    void RigidBody::ComputeInertia()
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_WRITE_LOCK(m_pxRigidActor->getScene());
            auto localPose = m_pxRigidActor->getCMassLocalPose().p;
            physx::PxRigidBodyExt::setMassAndUpdateInertia(*m_pxRigidActor, m_pxRigidActor->getMass(), &localPose);
        }
    }

    AZ::Vector3 RigidBody::GetLinearVelocity() const
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_READ_LOCK(m_pxRigidActor->getScene());
            return PxMathConvert(m_pxRigidActor->getLinearVelocity());
        }
        return AZ::Vector3::CreateZero();
    }

    void RigidBody::SetLinearVelocity(const AZ::Vector3& velocity)
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_WRITE_LOCK(m_pxRigidActor->getScene());
            m_pxRigidActor->setLinearVelocity(PxMathConvert(velocity));
        }
    }

    AZ::Vector3 RigidBody::GetAngularVelocity() const
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_READ_LOCK(m_pxRigidActor->getScene());
            return PxMathConvert(m_pxRigidActor->getAngularVelocity());
        }
        return AZ::Vector3::CreateZero();
    }

    void RigidBody::SetAngularVelocity(const AZ::Vector3& angularVelocity)
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_WRITE_LOCK(m_pxRigidActor->getScene());
            m_pxRigidActor->setAngularVelocity(PxMathConvert(angularVelocity));
        }
    }

    AZ::Vector3 RigidBody::GetLinearVelocityAtWorldPoint(const AZ::Vector3& worldPoint)
    {
        return m_pxRigidActor ?
               GetLinearVelocity() + GetAngularVelocity().Cross(worldPoint - GetCenterOfMassWorld()) :
               AZ::Vector3::CreateZero();
    }

    void RigidBody::ApplyLinearImpulse(const AZ::Vector3& impulse)
    {
        if (m_pxRigidActor)
        {
            physx::PxScene* scene = m_pxRigidActor->getScene();
            if (!scene)
            {
                AZ_Warning("PhysX Rigid Body", false, "ApplyLinearImpulse is only valid if the rigid body has been added to a scene. Name: %s", GetName().c_str());
                return;
            }

            if (IsKinematic())
            {
                AZ_Warning("PhysX Rigid Body", false, "ApplyLinearImpulse is only valid if the rigid body is not kinematic. Name: %s", GetName().c_str());
                return;
            }
            PHYSX_SCENE_WRITE_LOCK(scene);
            m_pxRigidActor->addForce(PxMathConvert(impulse), physx::PxForceMode::eIMPULSE);
        }
    }

    void RigidBody::ApplyLinearImpulseAtWorldPoint(const AZ::Vector3& impulse, const AZ::Vector3& worldPoint)
    {
        if (m_pxRigidActor)
        {
            if (IsKinematic())
            {
                AZ_Warning("PhysX Rigid Body", false, "ApplyLinearImpulseAtWorldPoint is only valid if the rigid body is not kinematic. Name: %s", GetName().c_str());
                return;
            }
            PHYSX_SCENE_WRITE_LOCK(m_pxRigidActor->getScene());
            physx::PxRigidBodyExt::addForceAtPos(*m_pxRigidActor, PxMathConvert(impulse), PxMathConvert(worldPoint), physx::PxForceMode::eIMPULSE);
        }
    }

    void RigidBody::ApplyAngularImpulse(const AZ::Vector3& angularImpulse)
    {
        if (m_pxRigidActor)
        {
            physx::PxScene* scene = m_pxRigidActor->getScene();
            if (!scene)
            {
                AZ_Warning("PhysX Rigid Body", false, "ApplyAngularImpulse is only valid if the rigid body has been added to a scene. Name: %s", GetName().c_str());
                return;
            }

            if (IsKinematic())
            {
                AZ_Warning("PhysX Rigid Body", false, "ApplyAngularImpulse is only valid if the rigid body is not kinematic. Name: %s", GetName().c_str());
                return;
            }

            PHYSX_SCENE_WRITE_LOCK(scene);
            m_pxRigidActor->addTorque(PxMathConvert(angularImpulse), physx::PxForceMode::eIMPULSE);
        }
    }

    void RigidBody::SetKinematic(bool isKinematic)
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_WRITE_LOCK(m_pxRigidActor->getScene());
            m_pxRigidActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, isKinematic);
        }
    }

    bool RigidBody::IsKinematic() const
    {
        bool result = false;

        if (m_pxRigidActor)
        {
            PHYSX_SCENE_READ_LOCK(m_pxRigidActor->getScene());
            auto rigidBodyFlags = m_pxRigidActor->getRigidBodyFlags();
            result = rigidBodyFlags.isSet(physx::PxRigidBodyFlag::eKINEMATIC);
        }

        return result;
    }

    void RigidBody::SetKinematicTarget(const AZ::Transform& targetTransform)
    {
        if (IsKinematic())
        {
            PHYSX_SCENE_WRITE_LOCK(m_pxRigidActor->getScene());
            m_pxRigidActor->setKinematicTarget(PxMathConvert(targetTransform));
        }
        else
        {
            AZ_Error("PhysX Rigid Body", false, "SetKinematicTarget is only valid if rigid body is kinematic. Name: %s", GetName().c_str());
        }
    }

    bool RigidBody::IsGravityEnabled() const
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_READ_LOCK(m_pxRigidActor->getScene());
            return m_pxRigidActor->getActorFlags().isSet(physx::PxActorFlag::eDISABLE_GRAVITY) == false;
        }
        return false;
    }

    void RigidBody::SetGravityEnabled(bool enabled)
    {
        {
            PHYSX_SCENE_WRITE_LOCK(m_pxRigidActor->getScene());
            m_pxRigidActor->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, enabled == false);
        }
        if (enabled)
        {
            ForceAwake();
        }
    }

    void RigidBody::SetSimulationEnabled(bool enabled)
    {
        PHYSX_SCENE_WRITE_LOCK(m_pxRigidActor->getScene());
        m_pxRigidActor->setActorFlag(physx::PxActorFlag::eDISABLE_SIMULATION, enabled == false);
    }

    void RigidBody::SetCCDEnabled(bool enabled)
    {
        PHYSX_SCENE_WRITE_LOCK(m_pxRigidActor->getScene());
        m_pxRigidActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, enabled);
    }

    // Physics::WorldBody
    Physics::World* RigidBody::GetWorld() const
    {
        return m_pxRigidActor ? Utils::GetUserData(m_pxRigidActor->getScene()) : nullptr;
    }

    AZ::Transform RigidBody::GetTransform() const
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_READ_LOCK(m_pxRigidActor->getScene());
            return PxMathConvert(m_pxRigidActor->getGlobalPose());
        }
        return AZ::Transform::CreateZero();
    }

    void RigidBody::SetTransform(const AZ::Transform& transform)
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_WRITE_LOCK(m_pxRigidActor->getScene());
            m_pxRigidActor->setGlobalPose(PxMathConvert(transform));
        }
    }

    AZ::Vector3 RigidBody::GetPosition() const
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_READ_LOCK(m_pxRigidActor->getScene());
            return PxMathConvert(m_pxRigidActor->getGlobalPose().p);
        }
        return AZ::Vector3::CreateZero();
    }

    AZ::Quaternion RigidBody::GetOrientation() const
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_READ_LOCK(m_pxRigidActor->getScene());
            return PxMathConvert(m_pxRigidActor->getGlobalPose().q);
        }
        return  AZ::Quaternion::CreateZero();
    }

    AZ::Aabb RigidBody::GetAabb() const
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_READ_LOCK(m_pxRigidActor->getScene());
            return PxMathConvert(m_pxRigidActor->getWorldBounds(1.0f));
        }
        return  AZ::Aabb::CreateNull();
    }

    AZ::EntityId RigidBody::GetEntityId() const
    {
        return m_actorUserData.GetEntityId();
    }

    void RigidBody::RayCast(const Physics::RayCastRequest& request, Physics::RayCastResult& result) const
    {
        AZ_Warning("PhysX Rigid Body", false, "RayCast not implemented.");
    }

    // Physics::ReferenceBase
    AZ::Crc32 RigidBody::GetNativeType() const
    {
        return PhysX::NativeTypeIdentifiers::RigidBody;
    }

    void* RigidBody::GetNativePointer() const
    {
        return m_pxRigidActor.get();
    }

    // Not in API but needed to support PhysicsComponentBus
    float RigidBody::GetLinearDamping() const
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_READ_LOCK(m_pxRigidActor->getScene());
            return m_pxRigidActor->getLinearDamping();
        }
        return 0.0f;
    }

    void RigidBody::SetLinearDamping(float damping)
    {
        if (damping < 0.0f)
        {
            AZ_Warning("PhysX Rigid Body", false, "Negative linear damping value (%6.4e). Name: %s", damping, GetName().c_str());
            return;
        }
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_WRITE_LOCK(m_pxRigidActor->getScene());
            m_pxRigidActor->setLinearDamping(damping);
        }
    }

    float RigidBody::GetAngularDamping() const
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_READ_LOCK(m_pxRigidActor->getScene());
            return m_pxRigidActor->getAngularDamping();
        }
        return 0.0f;
    }

    void RigidBody::SetAngularDamping(float damping)
    {
        if (damping < 0.0f)
        {
            AZ_Warning("PhysX Rigid Body", false, "Negative angular damping value (%6.4e). Name: %s", damping, GetName().c_str());
            return;
        }

        if (m_pxRigidActor)
        {
            PHYSX_SCENE_WRITE_LOCK(m_pxRigidActor->getScene());
            m_pxRigidActor->setAngularDamping(damping);
        }
    }

    bool RigidBody::IsAwake() const
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_READ_LOCK(m_pxRigidActor->getScene());
            return !m_pxRigidActor->isSleeping();
        }
        return false;
    }

    void RigidBody::ForceAsleep()
    {
        if (m_pxRigidActor) //<- Rigid body must be in a scene, otherwise putToSleep will crash
        {
            physx::PxScene* scene = m_pxRigidActor->getScene();
            if (scene)
            {
                PHYSX_SCENE_WRITE_LOCK(scene);
                m_pxRigidActor->putToSleep();
            }
        }
    }

    void RigidBody::ForceAwake()
    {
        if (m_pxRigidActor) //<- Rigid body must be in a scene, otherwise wakeUp will crash
        {
            physx::PxScene* scene = m_pxRigidActor->getScene();
            if (scene)
            {
                PHYSX_SCENE_WRITE_LOCK(scene);
                m_pxRigidActor->wakeUp();
            }
        }
    }

    float RigidBody::GetSleepThreshold() const
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_READ_LOCK(m_pxRigidActor->getScene());
            return m_pxRigidActor->getSleepThreshold();
        }
        return 0.0f;
    }

    void RigidBody::SetSleepThreshold(float threshold)
    {
        if (threshold < 0.0f)
        {
            AZ_Warning("PhysX Rigid Body", false, "Negative sleep threshold value (%6.4e). Name: %s", threshold, GetName().c_str());
            return;
        }

        if (m_pxRigidActor)
        {
            PHYSX_SCENE_WRITE_LOCK(m_pxRigidActor->getScene());
            m_pxRigidActor->setSleepThreshold(threshold);
        }
    }

    void RigidBody::AddToWorld(Physics::World& world)
    {
        physx::PxScene* scene = static_cast<physx::PxScene*>(world.GetNativePointer());

        if (!scene)
        {
            AZ_Error("RigidBody", false, "Tried to add body to invalid world.");
            return;
        }

        if (!m_pxRigidActor)
        {
            AZ_Error("RigidBody", false, "Tried to add invalid PhysX body to world.");
            return;
        }

        {
            PHYSX_SCENE_WRITE_LOCK(scene);
            scene->addActor(*m_pxRigidActor);
            if (m_startAsleep)
            {
                m_pxRigidActor->putToSleep();
            }
        }
    }

    void RigidBody::RemoveFromWorld(Physics::World& world)
    {
        physx::PxScene* scene = static_cast<physx::PxScene*>(world.GetNativePointer());
        if (!scene)
        {
            AZ_Error("PhysX World", false, "Tried to remove body from invalid world.");
            return;
        }

        if (!m_pxRigidActor)
        {
            AZ_Error("PhysX World", false, "Tried to remove invalid PhysX body from world.");
            return;
        }

        PHYSX_SCENE_WRITE_LOCK(scene);
        scene->removeActor(*m_pxRigidActor);
    }

    void RigidBody::SetName(const AZStd::string& entityName)
    {
        m_name = entityName;

        if (m_pxRigidActor)
        {
            PHYSX_SCENE_WRITE_LOCK(m_pxRigidActor->getScene());
            m_pxRigidActor->setName(m_name.c_str());
        }
    }

    const AZStd::string& RigidBody::GetName() const
    {
        return m_name;
    }

    void RigidBody::SetZeroCenterOfMass()
    {
        if (m_pxRigidActor)
        {
            PHYSX_SCENE_WRITE_LOCK(m_pxRigidActor->getScene());
            m_pxRigidActor->setCMassLocalPose(physx::PxTransform(PxMathConvert(AZ::Vector3::CreateZero())));
        }
    }
}
