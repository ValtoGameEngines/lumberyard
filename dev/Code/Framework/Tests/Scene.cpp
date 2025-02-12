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

#include <AzCore/UnitTest/TestTypes.h>

AZ_PUSH_DISABLE_WARNING(, "-Wdelete-non-virtual-dtor")

#include <FrameworkApplicationFixture.h>
#include <AzCore/Component/ComponentApplication.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/Component.h>
#include <AzFramework/Scene/SceneSystemComponent.h>
#include <AzFramework/Scene/Scene.h>
#include <AzCore/Asset/AssetManagerComponent.h>
#include <AzCore/Asset/AssetManager.h>
#include <AzCore/Memory/PoolAllocator.h>
#include <AzCore/Slice/SliceAssetHandler.h>
#include <AzFramework/IO/LocalFileIO.h>
#include <AzCore/Slice/SliceSystemComponent.h>

using namespace AzFramework;

// Test component that allows code to be injected into activate / deactivate for testing.

namespace SceneUnitTest
{

    class TestComponent;

    class TestComponentConfig : public AZ::ComponentConfig
    {
    public:
        AZ_RTTI(TestComponentConfig, "{DCD12D72-3BFE-43A9-9679-66B745814CAF}", ComponentConfig);

        typedef void(*ActivateFunction)(TestComponent* component);
        ActivateFunction m_activateFunction = nullptr;

        typedef void(*DeactivateFunction)(TestComponent* component);
        DeactivateFunction m_deactivateFunction = nullptr;
    };

    static const AZ::TypeId TestComponentTypeId = "{DC096267-4815-47D1-BA23-A1CDF0D72D9D}";
    class TestComponent : public AZ::Component
    {
    public:
        AZ_COMPONENT(TestComponent, TestComponentTypeId);

        static void Reflect(AZ::ReflectContext*) {};

        void Activate() override
        {
            if (m_config.m_activateFunction)
            {
                m_config.m_activateFunction(this);
            }
        }

        void Deactivate() override
        {
            if (m_config.m_deactivateFunction)
            {
                m_config.m_deactivateFunction(this);
            }
        }

        bool ReadInConfig(const AZ::ComponentConfig* baseConfig) override
        {
            if (auto config = azrtti_cast<const TestComponentConfig*>(baseConfig))
            {
                m_config = *config;
                return true;
            }
            return false;
        }

        bool WriteOutConfig(AZ::ComponentConfig* outBaseConfig) const override
        {
            if (auto outConfig = azrtti_cast<TestComponentConfig*>(outBaseConfig))
            {
                *outConfig = m_config;
                return true;
            }
            return false;
        }

        TestComponentConfig m_config;
    };

    // Fixture that creates a bare-bones app with only the system components necesary.

    class SceneTest
        : public UnitTest::ScopedAllocatorSetupFixture
    {
    public:
        void SetUp() override
        {
            AZ::AllocatorInstance<AZ::PoolAllocator>::Create();
            AZ::AllocatorInstance<AZ::ThreadPoolAllocator>::Create();

            m_prevFileIO = AZ::IO::FileIOBase::GetInstance();
            AZ::IO::FileIOBase::SetInstance(&m_fileIO);

            m_app.RegisterComponentDescriptor(SceneSystemComponent::CreateDescriptor());
            m_app.RegisterComponentDescriptor(AZ::SliceSystemComponent::CreateDescriptor());
            m_app.RegisterComponentDescriptor(AZ::AssetManagerComponent::CreateDescriptor());

            AZ::ComponentApplication::Descriptor desc;
            desc.m_enableDrilling = false; // the unit test framework already adds a driller
            m_systemEntity = m_app.Create(desc);
            m_systemEntity->Init();

            m_systemEntity->CreateComponent<SceneSystemComponent>();

            // Asset / slice system components needed by entity contexts
            m_systemEntity->CreateComponent<AZ::SliceSystemComponent>();
            m_systemEntity->CreateComponent<AZ::AssetManagerComponent>();
            m_systemEntity->Activate();
        }

        void TearDown() override
        {
            m_app.Destroy();

            AZ::IO::FileIOBase::SetInstance(m_prevFileIO);

            AZ::AllocatorInstance<AZ::PoolAllocator>::Destroy();
            AZ::AllocatorInstance<AZ::ThreadPoolAllocator>::Destroy();
        }

        AZ::IO::LocalFileIO m_fileIO;
        AZ::IO::FileIOBase* m_prevFileIO;
        AZ::ComponentApplication m_app;
        AZ::Entity* m_systemEntity = nullptr;

    };

    TEST_F(SceneTest, CreateScene)
    {
        Scene* scene = nullptr;
        AZ::Outcome<Scene*, AZStd::string> createSceneOutcome = AZ::Failure<AZStd::string>("");

        // A scene should be able to be created with a given name.
        AzFramework::SceneSystemRequestBus::BroadcastResult(createSceneOutcome, &AzFramework::SceneSystemRequestBus::Events::CreateScene, "TestScene");
        EXPECT_TRUE(createSceneOutcome.IsSuccess()) << "Unable to create a scene.";
    
        // The scene pointer returned should be valid
        scene = createSceneOutcome.GetValue();
        EXPECT_TRUE(scene != nullptr) << "Scene creation reported success, but no scene actually was actually returned.";

        // Attempting to create another scene with the same name should fail.
        createSceneOutcome = AZ::Failure<AZStd::string>("");
        AzFramework::SceneSystemRequestBus::BroadcastResult(createSceneOutcome, &AzFramework::SceneSystemRequestBus::Events::CreateScene, "TestScene");
        EXPECT_TRUE(!createSceneOutcome.IsSuccess()) << "Should not be able to create two scenes with the same name.";

    }

    TEST_F(SceneTest, GetScene)
    {
        Scene* createdScene = nullptr;
        Scene* retrievedScene = nullptr;
        Scene* nullScene = nullptr;
        const static AZStd::string_view s_sceneName = "TestScene";

        AZ::Outcome<Scene*, AZStd::string> createSceneOutcome = AZ::Failure<AZStd::string>("");
        AzFramework::SceneSystemRequestBus::BroadcastResult(createSceneOutcome, &AzFramework::SceneSystemRequestBus::Events::CreateScene, s_sceneName);
        createdScene = createSceneOutcome.GetValue();

        // Should be able to get a scene by name, and it should match the scene that was created.
        AzFramework::SceneSystemRequestBus::BroadcastResult(retrievedScene, &AzFramework::SceneSystemRequestBus::Events::GetScene, s_sceneName);
        EXPECT_TRUE(retrievedScene != nullptr) << "Attempting to get scene by name resulted in nullptr.";
        EXPECT_TRUE(retrievedScene == createdScene) << "Retrieved scene does not match created scene.";

        // An invalid name should return a null scene.
        AzFramework::SceneSystemRequestBus::BroadcastResult(nullScene, &AzFramework::SceneSystemRequestBus::Events::GetScene, "non-existant scene");
        EXPECT_TRUE(nullScene == nullptr) << "Should not be able to retrieve a scene that wasn't created.";
    }

    TEST_F(SceneTest, RemoveScene)
    {
        Scene* createdScene = nullptr;
        const static AZStd::string_view s_sceneName = "TestScene";

        AZ::Outcome<Scene*, AZStd::string> createSceneOutcome = AZ::Failure<AZStd::string>("");
        AzFramework::SceneSystemRequestBus::BroadcastResult(createSceneOutcome, &AzFramework::SceneSystemRequestBus::Events::CreateScene, s_sceneName);
        createdScene = createSceneOutcome.GetValue();

        bool success = false;
        AzFramework::SceneSystemRequestBus::BroadcastResult(success, &AzFramework::SceneSystemRequestBus::Events::RemoveScene, s_sceneName);
        EXPECT_TRUE(success) << "Failed to remove the scene that was just created.";

        success = true;
        AzFramework::SceneSystemRequestBus::BroadcastResult(success, &AzFramework::SceneSystemRequestBus::Events::RemoveScene, "non-existant scene");
        EXPECT_FALSE(success) << "Remove scene returned success for a non-existant scene.";
    }

    TEST_F(SceneTest, GetAllScenes)
    {
        constexpr size_t NumScenes = 5;

        Scene* scenes[NumScenes] = { nullptr };

        for (size_t i = 0; i < NumScenes; ++i)
        {
            AZ::Outcome<Scene*, AZStd::string> createSceneOutcome = AZ::Failure<AZStd::string>("");

            AZStd::string sceneName = AZStd::string::format("scene %u", i);
            AzFramework::SceneSystemRequestBus::BroadcastResult(createSceneOutcome, &AzFramework::SceneSystemRequestBus::Events::CreateScene, sceneName);
            scenes[i] = createSceneOutcome.GetValue();
        }

        AZStd::vector<Scene*> retrievedScenes;
        AzFramework::SceneSystemRequestBus::BroadcastResult(retrievedScenes, &AzFramework::SceneSystemRequestBus::Events::GetAllScenes);

        EXPECT_EQ(NumScenes, retrievedScenes.size()) << "GetAllScenes() returned a different number of scenes than those created.";

        for (size_t i = 0; i < NumScenes; ++i)
        {
            EXPECT_EQ(scenes[i], retrievedScenes.at(i)) << "GetAllScenes() returned scenes in a different order than they were created.";
        }
    }

    TEST_F(SceneTest, EntityContextSceneMapping)
    {
        // Create the entity context, entity, and component
        EntityContext* testEntityContext = new EntityContext();
        testEntityContext->InitContext();
        EntityContextId testEntityContextId = testEntityContext->GetContextId();
        AZ::Entity* testEntity = testEntityContext->CreateEntity("TestEntity");
        TestComponent* testComponent = testEntity->CreateComponent<TestComponent>();

        // Try to activate an entity and get the scene before a scene has been set. This should fail.
        TestComponentConfig failConfig;
        failConfig.m_activateFunction = [](TestComponent* component)
        {
            (void)component;
            Scene* scene = nullptr;
            EntityContextId entityContextId = EntityContextId::CreateNull();

            AzFramework::EntityIdContextQueryBus::BroadcastResult(entityContextId, &AzFramework::EntityIdContextQueryBus::Events::GetOwningContextId);

            // A null scene should be returned since a scene has not been set for this entity context.
            AzFramework::SceneSystemRequestBus::BroadcastResult(scene, &AzFramework::SceneSystemRequestBus::Events::GetSceneFromEntityContextId, entityContextId);
            EXPECT_TRUE(scene == nullptr) << "Found a scene when one shouldn't exist.";
        };
    
        testComponent->SetConfiguration(failConfig);
        testComponent->Activate();
        testComponent->Deactivate();

        // Create the scene
        AZ::Outcome<Scene*, AZStd::string> createSceneOutcome = AZ::Failure<AZStd::string>("");
        AzFramework::SceneSystemRequestBus::BroadcastResult(createSceneOutcome, &AzFramework::SceneSystemRequestBus::Events::CreateScene, "TestScene");
        Scene* scene = createSceneOutcome.GetValue();

        // Map the Entity context to the scene
        bool success = false;
        AzFramework::SceneSystemRequestBus::BroadcastResult(success, &AzFramework::SceneSystemRequestBus::Events::SetSceneForEntityContextId, testEntityContextId, scene);
        EXPECT_TRUE(success) << "Unable to associate an entity context with a scene.";
        AzFramework::SceneSystemRequestBus::BroadcastResult(success, &AzFramework::SceneSystemRequestBus::Events::SetSceneForEntityContextId, testEntityContextId, scene);
        EXPECT_FALSE(success) << "Attempting to map an entity context to a scene that's already mapped, this should not work.";

        // Now it should be possible to get the scene from the entity context within an Entity's Activate()
        TestComponentConfig successConfig;
        successConfig.m_activateFunction = [](TestComponent* component)
        {
            (void)component;
            Scene* scene = nullptr;
            EntityContextId entityContextId = EntityContextId::CreateNull();

            AzFramework::EntityIdContextQueryBus::BroadcastResult(entityContextId, &AzFramework::EntityIdContextQueryBus::Events::GetOwningContextId);

            // A scene should be returned since a scene has been set for this entity context.
            AzFramework::SceneSystemRequestBus::BroadcastResult(scene, &AzFramework::SceneSystemRequestBus::Events::GetSceneFromEntityContextId, entityContextId);
            EXPECT_TRUE(scene != nullptr) << "Could not find a scene for the entity context.";
        };

        testComponent->SetConfiguration(successConfig);
        testComponent->Activate();
        testComponent->Deactivate();

        // Now remove the entity context / scene association and make sure things fail again.
        success = false;
        AzFramework::SceneSystemRequestBus::BroadcastResult(success, &AzFramework::SceneSystemRequestBus::Events::RemoveSceneForEntityContextId, testEntityContextId, nullptr);
        EXPECT_FALSE(success) << "Should not be able to remove an entity context from a scene it's not associated with.";
        AzFramework::SceneSystemRequestBus::BroadcastResult(success, &AzFramework::SceneSystemRequestBus::Events::RemoveSceneForEntityContextId, testEntityContextId, scene);
        EXPECT_TRUE(success) << "Was not able to remove an entity context from a scene it's associated with.";

        testComponent->SetConfiguration(failConfig);
        testComponent->Activate();
        testComponent->Deactivate();

        delete testEntityContext; // This should also clean up owned entities / components.
    }

    // Test classes for use in the SceneSystem test. These can't be defined in the test itself due to some functions created by AZ_RTTI not having a body which breaks VS2015.
    class Foo1
    {
    public:
        AZ_RTTI(Foo1, "{9A6AA770-E2EA-4C5E-952A-341802E2DE58}");
    };
    class Foo2
    {
    public:
        AZ_RTTI(Foo2, "{916A2DB4-9C30-4B90-837E-2BC9855B474B}");
    };

    TEST_F(SceneTest, SceneSystem)
    {
        // Create the scene
        AZ::Outcome<Scene*, AZStd::string> createSceneOutcome = AZ::Failure<AZStd::string>("");
        AzFramework::SceneSystemRequestBus::BroadcastResult(createSceneOutcome, &AzFramework::SceneSystemRequestBus::Events::CreateScene, "TestScene");
        AzFramework::Scene* scene = createSceneOutcome.GetValue();

        // Set a class on the Scene
        Foo1* foo1a = new Foo1();
        EXPECT_TRUE(scene->SetSubsystem(foo1a));

        // Get that class back from the Scene
        EXPECT_EQ(foo1a, scene->GetSubsystem<Foo1>());

        // Try to set the same class type twice, this should fail.
        Foo1* foo1b = new Foo1();
        EXPECT_FALSE(scene->SetSubsystem(foo1b));
        delete foo1b;

        // Try to un-set a class that was never set, this should fail.
        EXPECT_FALSE(scene->UnsetSubsystem<Foo2>());

        // Unset the class that was previously set
        EXPECT_TRUE(scene->UnsetSubsystem<Foo1>());

        // Make sure that the previsouly set class was really removed.
        EXPECT_EQ(nullptr, scene->GetSubsystem<Foo1>());
    }
} // UnitTest

AZ_POP_DISABLE_WARNING
