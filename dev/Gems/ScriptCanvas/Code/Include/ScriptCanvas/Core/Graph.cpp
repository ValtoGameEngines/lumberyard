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

#include <stdarg.h>
#include <AzCore/Asset/AssetManager.h>
#include <AzCore/Component/EntityUtils.h>
#include <AzCore/Debug/Profiler.h>
#include <AzCore/Serialization/IdUtils.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Serialization/Utils.h>

#include <AzFramework/Entity/EntityContextBus.h>

#include <ScriptCanvas/Core/Connection.h>
#include <ScriptCanvas/Core/Core.h>
#include <ScriptCanvas/Core/Datum.h>
#include <ScriptCanvas/Core/Graph.h>
#include <ScriptCanvas/Core/Node.h>
#include <ScriptCanvas/Core/PureData.h>
#include <ScriptCanvas/Data/BehaviorContextObject.h>
#include <ScriptCanvas/Debugger/ValidationEvents/DataValidation/DataValidationIds.h>
#include <ScriptCanvas/Debugger/ValidationEvents/DataValidation/DataValidationEvents.h>
#include <ScriptCanvas/Debugger/ValidationEvents/ExecutionValidation/ExecutionValidationIds.h>
#include <ScriptCanvas/Debugger/ValidationEvents/ExecutionValidation/ExecutionValidationEvents.h>

#include <ScriptCanvas/Libraries/Core/UnaryOperator.h>
#include <ScriptCanvas/Libraries/Core/BinaryOperator.h>
#include <ScriptCanvas/Libraries/Core/EBusEventHandler.h>
#include <ScriptCanvas/Libraries/Core/ErrorHandler.h>
#include <ScriptCanvas/Libraries/Core/Start.h>
#include <ScriptCanvas/Libraries/Core/SendScriptEvent.h>
#include <ScriptCanvas/Libraries/Core/ReceiveScriptEvent.h>
#include <ScriptCanvas/Libraries/Core/ScriptEventBase.h>

#include <ScriptCanvas/Profiler/Driller.h>
#include <ScriptCanvas/Translation/Translation.h>
#include <ScriptCanvas/Debugger/StatusBus.h>
#include <ScriptCanvas/Variable/VariableBus.h>
#include <ScriptCanvas/Variable/VariableData.h>

namespace ScriptCanvas
{
    bool GraphComponentVersionConverter(AZ::SerializeContext& context, AZ::SerializeContext::DataElementNode& componentElementNode)
    {
        if (componentElementNode.GetVersion() < 12)
        {
            componentElementNode.RemoveElementByName(AZ_CRC("m_uniqueId", 0x52157a7a));
        }

        return true;
    }

    Graph::Graph(const ScriptCanvasId& scriptCanvasId)
        : m_scriptCanvasId(scriptCanvasId)
        , m_isObserved(false)
        , m_batchAddingData(false)
    {
    }

    Graph::~Graph()
    {
        GraphRequestBus::Handler::BusDisconnect(GetScriptCanvasId());
        const bool deleteData{ true };
        m_graphData.Clear(deleteData);
    }

    void Graph::Reflect(AZ::ReflectContext* context)
    {
        Data::PropertyMetadata::Reflect(context);
        Data::Type::Reflect(context);
        Nodes::UnaryOperator::Reflect(context);
        Nodes::UnaryExpression::Reflect(context);
        Nodes::BinaryOperator::Reflect(context);
        Nodes::ArithmeticExpression::Reflect(context);
        Nodes::BooleanExpression::Reflect(context);
        Nodes::EqualityExpression::Reflect(context);
        Nodes::ComparisonExpression::Reflect(context);
        Datum::Reflect(context);
        BehaviorContextObjectPtrReflect(context);

        GraphData::Reflect(context);

        AZ::SerializeContext* serializeContext = azrtti_cast<AZ::SerializeContext*>(context);
        if (serializeContext)
        {
            serializeContext->Class<Graph, AZ::Component>()
                ->Version(12, &GraphComponentVersionConverter)
                ->Field("m_graphData", &Graph::m_graphData)
                ;
        }
    }

    void Graph::Init()
    {
        const auto& scriptCanvasId = GetScriptCanvasId();
        GraphRequestBus::Handler::BusConnect(scriptCanvasId);
        RuntimeRequestBus::Handler::BusConnect(scriptCanvasId);

        for (auto& nodeEntity : m_graphData.m_nodes)
        {
            if (nodeEntity)
            {
                if (nodeEntity->GetState() == AZ::Entity::ES_CONSTRUCTED)
                {
                    nodeEntity->Init();
                }

                if (auto* node = AZ::EntityUtils::FindFirstDerivedComponent<Node>(nodeEntity))
                {
                    node->SetOwningScriptCanvasId(scriptCanvasId);

                    m_nodeMapping[node->GetEntityId()] = node;
                }
            }
        }

        m_graphData.BuildEndpointMap();
        for (auto& connectionEntity : m_graphData.m_connections)
        {
            if (connectionEntity)
            {
                if (connectionEntity->GetState() == AZ::Entity::ES_CONSTRUCTED)
                {
                    connectionEntity->Init();
                }
            }
        }

        StatusRequestBus::Handler::BusConnect(scriptCanvasId);
    }

    void Graph::Activate()
    {
        if (!m_executionContext.ActivateContext(GetScriptCanvasId()))
        {
            return;
        }

        RefreshConnectionValidity(true);

        AZ::EntityBus::Handler::BusConnect(GetEntityId());

        // If there are no nodes, there's nothing to do, deactivate the graph's entity.
        if (m_graphData.m_nodes.empty())
        {
            Deactivate();
            return;
        }

        AZ::SerializeContext* serializeContext{};
        AZ::ComponentApplicationBus::BroadcastResult(serializeContext, &AZ::ComponentApplicationRequests::GetSerializeContext);

        const bool replaceIdOnEntity{ true };
        const bool updateIdOnEntityId{ false };

        // Gather list of all the graph's node and connection entities
        AZStd::unordered_map<AZ::EntityId, AZ::EntityId> assetToRuntimeInternalMap;
        auto internalGraphEntityIdMapper = [&assetToRuntimeInternalMap](const AZ::EntityId& entityId, bool, const AZ::IdUtils::Remapper<AZ::EntityId>::IdGenerator)
        {
            // Add entity AZ::Entity::m_id instances to the map
            assetToRuntimeInternalMap.emplace(entityId, entityId);
            return entityId;
        };

        assetToRuntimeInternalMap[ScriptCanvas::GraphOwnerId] = GetEntityId();
        assetToRuntimeInternalMap[ScriptCanvas::UniqueId] = GetScriptCanvasId();
        assetToRuntimeInternalMap[GetEntityId()] = GetEntityId();
        assetToRuntimeInternalMap[GetScriptCanvasId()] = GetScriptCanvasId();
        assetToRuntimeInternalMap[AZ::EntityId()] = AZ::EntityId();

        AZ::IdUtils::Remapper<AZ::EntityId>::RemapIds(&m_graphData, internalGraphEntityIdMapper, serializeContext, replaceIdOnEntity);

        // Looks up the EntityContext loaded game entity map
        const AZStd::unordered_map<AZ::EntityId, AZ::EntityId>* loadedGameEntityIdMap = nullptr;

        AzFramework::EntityContextId owningContextId = AzFramework::EntityContextId::CreateNull();
        AzFramework::EntityIdContextQueryBus::EventResult(owningContextId, GetEntityId(), &AzFramework::EntityIdContextQueries::GetOwningContextId);
        if (!owningContextId.IsNull())
        {
            // Add a mapping for the GraphOwnerId to the execution component entity id
            AzFramework::EntityContextRequests* requests = AzFramework::EntityContextRequestBus::FindFirstHandler(owningContextId);

            if (requests)
            {
                loadedGameEntityIdMap = &requests->GetLoadedEntityIdMap();
            }
            else
            {
                static AZStd::unordered_map<AZ::EntityId, AZ::EntityId> s_emptyStaticMap;
                loadedGameEntityIdMap = &s_emptyStaticMap;
            }
        }

        // Lambda function remaps any known world map entities to their correct id other wise it DOES NOT remap the entityId.
        // This works differently than the runtime component remapping which remaps unknown world entities to invalid entity id
        auto worldEntityRemapper = [&assetToRuntimeInternalMap, &loadedGameEntityIdMap](const AZ::EntityId& entityId, bool, const AZ::IdUtils::Remapper<AZ::EntityId>::IdGenerator&) -> AZ::EntityId
        {
            auto foundEntityIdIt = assetToRuntimeInternalMap.find(entityId);
            if (foundEntityIdIt != assetToRuntimeInternalMap.end())
            {
                return foundEntityIdIt->second;
            }
            else
            {
                auto loadedIdIter = loadedGameEntityIdMap->find(entityId);

                if (loadedIdIter != loadedGameEntityIdMap->end())
                {
                    return loadedIdIter->second;
                }

                AZ_Warning("Script Canvas", false, "Entity Id %s is not part of the entity ids known by the graph. It will be not be remapped", entityId.ToString().data());
                return entityId;
            }
        };

        AZ::IdUtils::Remapper<AZ::EntityId>::ReplaceIdsAndIdRefs(&m_graphData, worldEntityRemapper, serializeContext);

        bool entryPointFound = false;

        for (auto& nodeEntity : m_graphData.m_nodes)
        {
            if (nodeEntity)
            {
                if (auto startNode = AZ::EntityUtils::FindFirstDerivedComponent<Nodes::Core::Start>(nodeEntity))
                {
                    m_executionContext.AddToExecutionStack(*startNode, SlotId());
                    entryPointFound = true;
                }
                else if (!entryPointFound)
                {
                    auto nodes = AZ::EntityUtils::FindDerivedComponents<ScriptCanvas::Node>(nodeEntity);
                    for (auto iter = nodes.begin(); iter != nodes.end() && !entryPointFound; ++iter)
                    {
                        entryPointFound = (*iter)->IsEntryPoint();
                    }
                }
            }
        }

        // If we still can't find a start node, there's nothing to do.
        if (!entryPointFound)
        {
            AZ_Warning("Script Canvas", false, "Graph does not have any entry point nodes, it will not run.");
            Deactivate();
            return;
        }

        for (auto& nodeEntity : m_graphData.m_nodes)
        {
            // Restore once we have Error Handling figured out
            if (auto errorHandlerNode = nodeEntity ? AZ::EntityUtils::FindFirstDerivedComponent<Nodes::Core::ErrorHandler>(nodeEntity) : nullptr)
            {
                AZStd::vector<AZStd::pair<Node*, const SlotId>> errorSources = errorHandlerNode->GetSources();

                if (errorSources.empty())
                {
                    m_executionContext.AddErrorHandler(m_scriptCanvasId, errorHandlerNode->GetEntityId());
                }
                else
                {
                    for (auto errorNodes : errorSources)
                    {
                        m_executionContext.AddErrorHandler(errorNodes.first->GetEntityId(), errorHandlerNode->GetEntityId());
                    }
                }
            }

            if (nodeEntity)
            {
                if (nodeEntity->GetState() == AZ::Entity::ES_INIT)
                {
                    nodeEntity->Activate();
                }
            }
        }

        for (auto& connectionEntity : m_graphData.m_connections)
        {
            if (connectionEntity)
            {
                if (connectionEntity->GetState() == AZ::Entity::ES_INIT)
                {
                    connectionEntity->Activate();
                }
            }
        }

        PostActivate();
    }

    void Graph::Deactivate()
    {
        m_executionContext.DeactivateContext();
        AZ::EntityBus::Handler::BusDisconnect(GetEntityId());

        for (auto& nodeEntity : m_graphData.m_nodes)
        {
            if (nodeEntity)
            {
                if (nodeEntity->GetState() == AZ::Entity::ES_ACTIVE)
                {
                    nodeEntity->Deactivate();
                }
            }
        }

        for (auto& connectionEntity : m_graphData.m_connections)
        {
            if (connectionEntity)
            {
                if (connectionEntity->GetState() == AZ::Entity::ES_ACTIVE)
                {
                    connectionEntity->Deactivate();
                }
            }
        }
    }

    bool Graph::AddItem(AZ::Entity* itemRef)
    {
        AZ::Entity* elementEntity = itemRef;
        if (!elementEntity)
        {
            return false;
        }

        if (elementEntity->GetState() == AZ::Entity::ES_CONSTRUCTED)
        {
            elementEntity->Init();
        }

        if (auto* node = AZ::EntityUtils::FindFirstDerivedComponent<Node>(elementEntity))
        {
            return AddNode(elementEntity->GetId());
        }

        if (auto* connection = AZ::EntityUtils::FindFirstDerivedComponent<Connection>(elementEntity))
        {
            return AddConnection(elementEntity->GetId());
        }

        return false;
    }

    bool Graph::RemoveItem(AZ::Entity* itemRef)
    {
        if (AZ::EntityUtils::FindFirstDerivedComponent<Node>(itemRef))
        {
            return RemoveNode(itemRef->GetId());
        }
        else if (AZ::EntityUtils::FindFirstDerivedComponent<Connection>(itemRef))
        {
            return RemoveConnection(itemRef->GetId());
        }

        return false;
    }

    void Graph::ValidateGraph(ValidationResults& validationResults)
    {
        validationResults.ClearResults();

        for (auto& connectionEntity : m_graphData.m_connections)
        {
            auto outcome = ValidateConnection(connectionEntity);

            if (!outcome.IsSuccess())
            {
                if (Connection* connection = AZ::EntityUtils::FindFirstDerivedComponent<Connection>(connectionEntity))
                {
                    ValidationEvent* validationEvent = nullptr;

                    if (outcome.GetError().m_validationEventId == DataValidationIds::UnknownTargetEndpointCrc)
                    {
                        validationEvent = aznew UnknownTargetEndpointEvent(connection->GetTargetEndpoint());
                    }
                    else if (outcome.GetError().m_validationEventId == DataValidationIds::UnknownSourceEndpointCrc)
                    {
                        validationEvent = aznew UnknownSourceEndpointEvent(connection->GetSourceEndpoint());
                    }
                    else if (outcome.GetError().m_validationEventId == DataValidationIds::ScopedDataConnectionCrc)
                    {
                        validationEvent = aznew ScopedDataConnectionEvent(connection->GetEntityId());
                    }

                    if (validationEvent)
                    {
                        validationEvent->SetDescription(outcome.GetError().m_errorDescription);
                        validationResults.m_validationEvents.push_back(validationEvent);
                    }
                }
            }
        }

        for (auto& nodeEntity : m_graphData.m_nodes)
        {
            auto outcome = ValidateNode(nodeEntity, validationResults);

            if (!outcome.IsSuccess())
            {
                const auto& validationErrors = outcome.GetError();

                for (const auto& validationStruct : validationErrors)
                {
                    if (Node* node = AZ::EntityUtils::FindFirstDerivedComponent<Node>(nodeEntity))
                    {
                        ValidationEvent* validationEvent = nullptr;

                        if (validationStruct.m_validationEventId == ExecutionValidationIds::UnusedNodeCrc)
                        {
                            validationEvent = aznew UnusedNodeEvent(node->GetEntityId());
                        }

                        if (validationEvent)
                        {
                            validationEvent->SetDescription(validationStruct.m_errorDescription);
                            validationResults.m_validationEvents.push_back(validationEvent);
                        }
                    }
                }
            }
        }

        ValidateVariables(validationResults);
        ValidateScriptEvents(validationResults);
    }

    void Graph::PostActivate()
    {
        GraphConfigurationNotificationBus::Event(GetEntityId(), &GraphConfigurationNotifications::ConfigureScriptCanvasId, GetScriptCanvasId());

        m_variableRequests = GraphVariableManagerRequestBus::FindFirstHandler(GetScriptCanvasId());

        for (auto& nodePair : m_nodeMapping)
        {
            nodePair.second->PostActivate();
        }
    }

    void Graph::ValidateVariables(ValidationResults& validationResults)
    {
        const VariableData* variableData = GetVariableData();
        
        if (!variableData)
        {
            return;
        }

        for (const auto& variable : variableData->GetVariables())
        {
            const VariableId& variableId = variable.first;
            Data::Type variableType = GetVariableType(variableId);

            AZStd::string errorDescription;

            if (variableType.GetType() == Data::eType::BehaviorContextObject)
            {
                AZ::BehaviorContext* behaviorContext{};
                AZ::ComponentApplicationBus::BroadcastResult(behaviorContext, &AZ::ComponentApplicationRequests::GetBehaviorContext);

                if (behaviorContext->m_typeToClassMap.find(variableType.GetAZType()) == behaviorContext->m_typeToClassMap.end())
                {
                    errorDescription = AZStd::string::format("Variable %s has an invalid type %s.", GetVariableName(variableId).data(), variableType.GetAZType().ToString<AZStd::string>().c_str());
                }
            } 
            else if (variableType == Data::Type::Invalid())
            {
                errorDescription = AZStd::string::format("Variable %s has an invalid type.", GetVariableName(variableId).data());
            }

            if (!errorDescription.empty())
            {
                ValidationEvent* validationEvent = aznew InvalidVariableTypeEvent(variableId);
                validationEvent->SetDescription(errorDescription);
                validationResults.m_validationEvents.push_back(validationEvent);
            }
        }
    }

    void Graph::ValidateScriptEvents(ValidationResults& validationResults)
    {
        for (auto& nodeEntity : m_graphData.m_nodes)
        {
            if (nodeEntity)
            {
                ValidationEvent* validationEvent = nullptr;
                if (auto scriptEventNode = AZ::EntityUtils::FindFirstDerivedComponent<Nodes::Core::Internal::ScriptEventBase>(nodeEntity))
                {
                    AZ::Data::Asset<ScriptEvents::ScriptEventsAsset> assetData = AZ::Data::AssetManager::Instance().GetAsset<ScriptEvents::ScriptEventsAsset>(scriptEventNode->GetAssetId());
                    if (assetData)
                    {
                        ScriptEvents::ScriptEvent& definition = assetData.Get()->m_definition;

                        if (scriptEventNode->GetVersion() != definition.GetVersion())
                        {
                            validationEvent = aznew ScriptEventVersionMismatch(scriptEventNode->GetVersion(), scriptEventNode->GetScriptEvent(), nodeEntity->GetId());
                            validationResults.m_validationEvents.push_back(validationEvent);
                        }
                    }
                }
            }
        }
    }

    void Graph::ReportError(const Node& node, const AZStd::string& errorSource, const AZStd::string& errorMessage)
    {
        AZStd::string annotatedErrorMessage = AZStd::string::format("%s - %s", errorSource.c_str(), errorMessage.c_str());
        m_executionContext.ReportError(node, annotatedErrorMessage.c_str());
    }

    bool Graph::AddNode(const AZ::EntityId& nodeId)
    {
        if (nodeId.IsValid())
        {
            auto entry = AZStd::find_if(m_graphData.m_nodes.begin(), m_graphData.m_nodes.end(), [nodeId](const AZ::Entity* node) { return node->GetId() == nodeId; });
            if (entry == m_graphData.m_nodes.end())
            {
                AZ::Entity* nodeEntity = nullptr;
                AZ::ComponentApplicationBus::BroadcastResult(nodeEntity, &AZ::ComponentApplicationRequests::FindEntity, nodeId);
                AZ_Assert(nodeEntity, "Failed to add node to Graph, did you initialize the node entity?");
                if (nodeEntity)
                {
                    auto node = AZ::EntityUtils::FindFirstDerivedComponent<Node>(nodeEntity);
                    if (node)
                    {
                        m_graphData.m_nodes.emplace(nodeEntity);
                        m_nodeMapping[nodeId] = node;
                        
                        node->SetOwningScriptCanvasId(m_scriptCanvasId);
                        node->Configure();
                        GraphNotificationBus::Event(m_scriptCanvasId, &GraphNotifications::OnNodeAdded, nodeId);
                        return true;
                    }

                }
            }
        }
        return false;
    }

    bool Graph::RemoveNode(const AZ::EntityId& nodeId)
    {
        if (nodeId.IsValid())
        {
            Node* node = FindNode(nodeId);
            if (node)
            {
                auto entry = m_graphData.m_nodes.find(node->GetEntity());
            if (entry != m_graphData.m_nodes.end())
            {
                m_nodeMapping.erase(nodeId);
                m_graphData.m_nodes.erase(entry);
                GraphNotificationBus::Event(GetScriptCanvasId(), &GraphNotifications::OnNodeRemoved, nodeId);                

                RemoveDependentAsset(nodeId);
                return true;
            }
        }
        }
        return false;
    }

    Node* Graph::FindNode(AZ::EntityId nodeID) const
    {
        auto nodeIter = m_nodeMapping.find(nodeID);

        if (nodeIter == m_nodeMapping.end())
        {
            return nullptr;
        }

        return nodeIter->second;
    }

    AZStd::vector<AZ::EntityId> Graph::GetNodes() const
    {
        AZStd::vector<AZ::EntityId> entityIds;
        for (auto& nodeRef : m_graphData.m_nodes)
        {
            entityIds.push_back(nodeRef->GetId());
        }

        return entityIds;
    }

    const AZStd::vector<AZ::EntityId> Graph::GetNodesConst() const
    {
        return GetNodes();
    }

    Slot* Graph::FindSlot(const ScriptCanvas::Endpoint& endpoint) const
    {
        Node* node = FindNode(endpoint.GetNodeId());

        if (node)
        {
            return node->GetSlot(endpoint.GetSlotId());
        }

        return nullptr;
    }

    bool Graph::AddConnection(const AZ::EntityId& connectionId)
    {
        if (connectionId.IsValid())
        {
            auto entry = AZStd::find_if(m_graphData.m_connections.begin(), m_graphData.m_connections.end(), [connectionId](const AZ::Entity* connection) { return connection->GetId() == connectionId; });
            if (entry == m_graphData.m_connections.end())
            {
                AZ::Entity* connectionEntity{};
                AZ::ComponentApplicationBus::BroadcastResult(connectionEntity, &AZ::ComponentApplicationRequests::FindEntity, connectionId);
                auto connection = connectionEntity ? AZ::EntityUtils::FindFirstDerivedComponent<Connection>(connectionEntity) : nullptr;
                AZ_Warning("Script Canvas", connection, "Failed to add connection to Graph, did you initialize the connection entity?");
                if (connection)
                {
                    m_graphData.m_connections.emplace_back(connectionEntity);
                    m_graphData.m_endpointMap.emplace(connection->GetSourceEndpoint(), connection->GetTargetEndpoint());
                    m_graphData.m_endpointMap.emplace(connection->GetTargetEndpoint(), connection->GetSourceEndpoint());
                    GraphNotificationBus::Event(GetScriptCanvasId(), &GraphNotifications::OnConnectionAdded, connectionId);

                    if (connection->GetSourceEndpoint().IsValid())
                    {
                        EndpointNotificationBus::Event(connection->GetSourceEndpoint(), &EndpointNotifications::OnEndpointConnected, connection->GetTargetEndpoint());
                    }
                    if (connection->GetTargetEndpoint().IsValid())
                    {
                        EndpointNotificationBus::Event(connection->GetTargetEndpoint(), &EndpointNotifications::OnEndpointConnected, connection->GetSourceEndpoint());
                    }

                    return true;
                }
            }
        }
        return false;
    }

    bool Graph::RemoveConnection(const AZ::EntityId& connectionId)
    {
        if (connectionId.IsValid())
        {
            auto entry = AZStd::find_if(m_graphData.m_connections.begin(), m_graphData.m_connections.end(), [connectionId](const AZ::Entity* connection) { return connection->GetId() == connectionId; });
            if (entry != m_graphData.m_connections.end())
            {
                auto connection = *entry ? AZ::EntityUtils::FindFirstDerivedComponent<Connection>(*entry) : nullptr;
                if (connection)
                {
                    const ScriptCanvas::Endpoint& sourceEndpoint = connection->GetSourceEndpoint();
                    const ScriptCanvas::Endpoint& targetEndpoint = connection->GetTargetEndpoint();

                    auto sourceRange = m_graphData.m_endpointMap.equal_range(sourceEndpoint);

                    for (auto mapIter = sourceRange.first; mapIter != sourceRange.second; ++mapIter)
                    {
                        if (mapIter->second == targetEndpoint)
                        {
                            m_graphData.m_endpointMap.erase(mapIter);
                            break;
                        }
                    }

                    auto targetRange = m_graphData.m_endpointMap.equal_range(targetEndpoint);

                    for (auto mapIter = targetRange.first; mapIter != targetRange.second; ++mapIter)
                    {
                        if (mapIter->second == sourceEndpoint)
                        {
                            m_graphData.m_endpointMap.erase(mapIter);
                            break;
                        }
                    }
                }
                m_graphData.m_connections.erase(entry);
                GraphNotificationBus::Event(GetScriptCanvasId(), &GraphNotifications::OnConnectionRemoved, connectionId);

                if (connection->GetSourceEndpoint().IsValid())
                {
                    EndpointNotificationBus::Event(connection->GetSourceEndpoint(), &EndpointNotifications::OnEndpointDisconnected, connection->GetTargetEndpoint());
                }
                if (connection->GetTargetEndpoint().IsValid())
                {
                    EndpointNotificationBus::Event(connection->GetTargetEndpoint(), &EndpointNotifications::OnEndpointDisconnected, connection->GetSourceEndpoint());
                }

                return true;
            }
        }
        return false;
    }

    AZStd::vector<AZ::EntityId> Graph::GetConnections() const
    {
        AZStd::vector<AZ::EntityId> entityIds;
        for (auto& connectionRef : m_graphData.m_connections)
        {
            entityIds.push_back(connectionRef->GetId());
        }

        return entityIds;
    }

    AZStd::vector<Endpoint> Graph::GetConnectedEndpoints(const Endpoint& firstEndpoint) const
    {
        AZStd::vector<Endpoint> connectedEndpoints;
        auto otherEndpointsRange = m_graphData.m_endpointMap.equal_range(firstEndpoint);
        for (auto otherIt = otherEndpointsRange.first; otherIt != otherEndpointsRange.second; ++otherIt)
        {
            connectedEndpoints.push_back(otherIt->second);
        }
        return connectedEndpoints;
    }

    AZStd::pair< EndpointMapConstIterator, EndpointMapConstIterator > Graph::GetConnectedEndpointIterators(const Endpoint& firstEndpoint) const
    {
        return m_graphData.m_endpointMap.equal_range(firstEndpoint);
    }

    bool Graph::IsEndpointConnected(const Endpoint& endpoint) const
    {
        size_t connectionCount = m_graphData.m_endpointMap.count(endpoint);
        return connectionCount > 0;
    }

    bool Graph::FindConnection(AZ::Entity*& connectionEntity, const Endpoint& firstEndpoint, const Endpoint& otherEndpoint) const
    {
        if (!firstEndpoint.IsValid() || !otherEndpoint.IsValid())
        {
            return false;
        }

        AZ::Entity* foundEntity{};
        for (auto connectionRefIt = m_graphData.m_connections.begin(); connectionRefIt != m_graphData.m_connections.end(); ++connectionRefIt)
        {
            auto* connection = *connectionRefIt ? AZ::EntityUtils::FindFirstDerivedComponent<Connection>(*connectionRefIt) : nullptr;
            if (connection)
            {
                if ((connection->GetSourceEndpoint() == firstEndpoint && connection->GetTargetEndpoint() == otherEndpoint)
                    || (connection->GetSourceEndpoint() == otherEndpoint && connection->GetTargetEndpoint() == firstEndpoint))
                {
                    foundEntity = connection->GetEntity();
                    break;
                }
            }
        }

        if (foundEntity)
        {
            connectionEntity = foundEntity;
            return true;
        }

        return false;
    }


    bool Graph::Connect(const AZ::EntityId& sourceNodeId, const SlotId& sourceSlotId, const AZ::EntityId& targetNodeId, const SlotId& targetSlotId)
    {
        return ConnectByEndpoint({ sourceNodeId, sourceSlotId }, { targetNodeId, targetSlotId });
    }

    bool Graph::ConnectByEndpoint(const Endpoint& sourceEndpoint, const Endpoint& targetEndpoint)
    {
        AZ::Outcome<void, AZStd::string> outcome = CanCreateConnectionBetween(sourceEndpoint, targetEndpoint);

        if (outcome.IsSuccess())
        {
            auto* connectionEntity = aznew AZ::Entity("Connection");
            connectionEntity->CreateComponent<Connection>(sourceEndpoint, targetEndpoint);


            AZ::Entity* nodeEntity{};
            AZ::ComponentApplicationBus::BroadcastResult(nodeEntity, &AZ::ComponentApplicationRequests::FindEntity, sourceEndpoint.GetNodeId());
            auto node = nodeEntity ? AZ::EntityUtils::FindFirstDerivedComponent<Node>(nodeEntity) : nullptr;
            AZStd::string sourceNodeName = node ? node->GetNodeName() : "";
            AZStd::string sourceSlotName = node ? node->GetSlotName(sourceEndpoint.GetSlotId()) : "";

            nodeEntity = {};
            AZ::ComponentApplicationBus::BroadcastResult(nodeEntity, &AZ::ComponentApplicationRequests::FindEntity, targetEndpoint.GetNodeId());
            node = nodeEntity ? AZ::EntityUtils::FindFirstDerivedComponent<Node>(nodeEntity) : nullptr;
            AZStd::string targetNodeName = node ? node->GetNodeName() : "";
            AZStd::string targetSlotName = node ? node->GetSlotName(targetEndpoint.GetSlotId()) : "";
            connectionEntity->SetName(AZStd::string::format("srcEndpoint=(%s: %s), destEndpoint=(%s: %s)",
                sourceNodeName.data(),
                sourceSlotName.data(),
                targetNodeName.data(),
                targetSlotName.data()));

            connectionEntity->Init();
            connectionEntity->Activate();

            return AddConnection(connectionEntity->GetId());
        }
        else
        {
            AZ_Warning("Script Canvas", false, "Failed to create connection: %s", outcome.GetError().c_str());
        }

        return false;
    }

    bool Graph::AddDependentAsset(AZ::EntityId nodeId, const AZ::TypeId assetType, const AZ::Data::AssetId assetId)
    {
        AZ::Entity* nodeEntity = nullptr;
        AZ::ComponentApplicationBus::BroadcastResult(nodeEntity, &AZ::ComponentApplicationRequests::FindEntity, nodeId);
        AZ_Assert(nodeEntity, "Failed to add node to Graph, did you initialize the node entity?");
        if (nodeEntity)
        {
            auto node = AZ::EntityUtils::FindFirstDerivedComponent<Node>(nodeEntity);
            if (node)
            {
                if (Nodes::Core::Internal::ScriptEventBase* scriptEventBase = azrtti_cast<Nodes::Core::Internal::ScriptEventBase*>(node))
                {
                    if (m_graphData.m_dependentAssets.find(scriptEventBase->GetAssetId()) == m_graphData.m_dependentAssets.end())
                    {
                        m_graphData.m_scriptEventAssets.push_back(AZStd::make_pair(nodeId, scriptEventBase->GetAsset()));
                    }
                    return true;
                }
            }
        }
        return false;
    }

    bool Graph::RemoveDependentAsset(AZ::EntityId nodeId)
    {
        auto assetIter = AZStd::find_if(m_graphData.m_scriptEventAssets.begin(), m_graphData.m_scriptEventAssets.end(), [nodeId](const GraphData::DependentScriptEvent::value_type& value) { return nodeId == value.first; });
        if (assetIter != m_graphData.m_scriptEventAssets.end())
            {
            assetIter->second = {};
            m_graphData.m_scriptEventAssets.erase(assetIter);
                return true;
            }

        return false;
    }

    bool Graph::IsInDataFlowPath(const Node* sourceNode, const Node* targetNode) const
    {
        return sourceNode && sourceNode->IsTargetInDataFlowPath(targetNode);
    }

    AZ::Outcome<void, AZStd::vector<Graph::ValidationStruct>> Graph::ValidateNode(AZ::Entity* nodeEntity, ValidationResults& validationEvents) const
    {
        AZStd::vector< ValidationStruct > errorResults;

        ScriptCanvas::Node* nodeComponent = AZ::EntityUtils::FindFirstDerivedComponent<ScriptCanvas::Node>(nodeEntity);

        if (nodeComponent == nullptr)
        {
            errorResults.emplace_back();
            return AZ::Failure(errorResults);
        }

        // If the node is disabled. Just ignore any validation issues that it might throw.
        if (!nodeComponent->IsNodeEnabled())
        {
            return AZ::Success();
        }

        if (!nodeComponent->ValidateNode(validationEvents))
        {
            ValidationStruct validationStruct;
            validationStruct.m_validationEventId = DataValidationIds::InternalValidationErrorCrc;

            errorResults.emplace_back(validationStruct);
        }

        if (!nodeComponent->IsEntryPoint())
        {
            if (nodeComponent->FindConnectedNodesByDescriptor(SlotDescriptors::ExecutionIn()).empty())
            {
                ValidationStruct validationStruct;
                validationStruct.m_validationEventId = ExecutionValidationIds::UnusedNodeCrc;
                validationStruct.m_errorDescription = AZStd::string::format("Node (%s) will not be triggered during graph execution", nodeComponent->GetNodeName().c_str());

                errorResults.emplace_back(validationStruct);
            }
        }

        if (errorResults.empty())
        {
            return AZ::Success();
        }
        else
        {
            return AZ::Failure(errorResults);
        }
    }

    AZ::Outcome<void, Graph::ValidationStruct> Graph::ValidateConnection(AZ::Entity* connectionEntity) const
    {
        ScriptCanvas::Connection* connectionComponent = AZ::EntityUtils::FindFirstDerivedComponent<ScriptCanvas::Connection>(connectionEntity);

        if (connectionComponent == nullptr)
        {
            return AZ::Failure(ValidationStruct());
        }

        Endpoint sourceEndpoint = connectionComponent->GetSourceEndpoint();
        Endpoint targetEndpoint = connectionComponent->GetTargetEndpoint();

        auto sourceEntity = AZStd::find_if(m_graphData.m_nodes.begin(), m_graphData.m_nodes.end(), [&sourceEndpoint](const AZ::Entity* node) { return node->GetId() == sourceEndpoint.GetNodeId(); });
        if (sourceEntity == m_graphData.m_nodes.end())
        {
            ValidationStruct validationStruct;
            validationStruct.m_validationEventId = DataValidationIds::UnknownSourceEndpointCrc;
            validationStruct.m_errorDescription = AZStd::string::format("The source node with id %s is not a part of this graph, a connection cannot be made", sourceEndpoint.GetNodeId().ToString().data());

            return AZ::Failure(validationStruct);
        }

        auto targetEntity = AZStd::find_if(m_graphData.m_nodes.begin(), m_graphData.m_nodes.end(), [&targetEndpoint](const AZ::Entity* node) { return node->GetId() == targetEndpoint.GetNodeId(); });
        if (targetEntity == m_graphData.m_nodes.end())
        {
            ValidationStruct validationStruct;
            validationStruct.m_validationEventId = DataValidationIds::UnknownTargetEndpointCrc;
            validationStruct.m_errorDescription = AZStd::string::format("The target node with id %s is not a part of this graph, a connection cannot be made", targetEndpoint.GetNodeId().ToString().data());

            return AZ::Failure(validationStruct);
        }

        Node* sourceNode = AZ::EntityUtils::FindFirstDerivedComponent<Node>((*sourceEntity));
        Node* targetNode = AZ::EntityUtils::FindFirstDerivedComponent<Node>((*targetEntity));

        Slot* sourceSlot = sourceNode ? sourceNode->GetSlot(sourceEndpoint.GetSlotId()) : nullptr;
        Slot* targetSlot = targetNode ? targetNode->GetSlot(targetEndpoint.GetSlotId()) : nullptr;

        if (sourceSlot == nullptr)
        {
            ValidationStruct validationStruct;
            validationStruct.m_validationEventId = DataValidationIds::UnknownSourceEndpointCrc;
            validationStruct.m_errorDescription = AZStd::string::format("Source Slot could not be found on Node %s", (*sourceEntity)->GetName().c_str());

            return AZ::Failure(validationStruct);
        }
        else if (targetSlot == nullptr)
        {
            ValidationStruct validationStruct;
            validationStruct.m_validationEventId = DataValidationIds::UnknownTargetEndpointCrc;
            validationStruct.m_errorDescription = AZStd::string::format("Target Slot could not be found on Node %s", (*targetEntity)->GetName().c_str());

            return AZ::Failure(validationStruct);
        }

        if (sourceSlot->IsData())
        {
            return ValidateDataConnection((*sourceNode), (*sourceSlot), (*targetNode), (*targetSlot));
        }
        else
        {
            return ValidateExecutionConnection((*sourceNode), (*sourceSlot), (*targetNode), (*targetSlot));
        }
    }

    AZ::Outcome<void, Graph::ValidationStruct> Graph::ValidateExecutionConnection(const Node& sourceNode, const Slot& sourceSlot, const Node& targetNode, const Slot& targetSlot) const
    {
        // Nothing special to validate for Execution Connections yet
        return AZ::Success();
    }

    AZ::Outcome<void, Graph::ValidationStruct> Graph::ValidateDataConnection(const Node& sourceNode, const Slot& sourceSlot, const Node& targetNode, const Slot& targetSlot) const
    {
        if (sourceSlot.IsData() && sourceSlot.GetDescriptor().CanConnectTo(targetSlot.GetDescriptor()))
        {
            bool isInDataFlow = false;

            if (sourceSlot.IsInput())
            {
                isInDataFlow = IsInDataFlowPath(&targetNode, &sourceNode);
            }
            else
            {
                isInDataFlow = IsInDataFlowPath(&sourceNode, &targetNode);
            }

            if (!isInDataFlow)
            {
                ValidationStruct validationStruct;
                validationStruct.m_validationEventId = DataValidationIds::ScopedDataConnectionCrc;
                validationStruct.m_errorDescription = AZStd::string::format("There is an invalid data connection %s.%s --> %s.%s, the data is not in the execution path between nodes. Either route execution %s --> %s, or store the data in a variable if it is needed."
                                                                            , sourceNode.GetNodeName().data()
                                                                            , sourceSlot.GetName().data()
                                                                            , targetNode.GetNodeName().data()
                                                                            , targetSlot.GetName().data()
                                                                            , sourceNode.GetNodeName().data()
                                                                            , targetNode.GetNodeName().data());

                return AZ::Failure(validationStruct);
            }
        }

        return AZ::Success();
    }

    AZ::Outcome<void, AZStd::string> Graph::CanCreateConnectionBetween(const Endpoint& sourceEndpoint, const Endpoint& targetEndpoint) const
    {
        AZ::Entity* foundEntity = nullptr;
        if (FindConnection(foundEntity, sourceEndpoint, targetEndpoint))
        {
            return AZ::Failure(AZStd::string::format("Attempting to create duplicate connection between source endpoint (%s, %s) and target endpoint(%s, %s)",
                sourceEndpoint.GetNodeId().ToString().data(), sourceEndpoint.GetSlotId().m_id.ToString<AZStd::string>().data(),
                targetEndpoint.GetNodeId().ToString().data(), targetEndpoint.GetSlotId().m_id.ToString<AZStd::string>().data()));
        }

        return CanConnectionExistBetween(sourceEndpoint, targetEndpoint);
    }

    AZ::Outcome<void, AZStd::string> Graph::CanConnectionExistBetween(const Endpoint& sourceEndpoint, const Endpoint& targetEndpoint) const
    {
        auto sourceNode = FindNode(sourceEndpoint.GetNodeId());
        if (sourceNode == nullptr)
        {
            return AZ::Failure(AZStd::string::format("The source node with id %s is not a part of this graph, a connection cannot be made", sourceEndpoint.GetNodeId().ToString().data()));
        }

        Slot* sourceSlot = sourceNode->GetSlot(sourceEndpoint.GetSlotId());

        if (sourceSlot == nullptr)
        {
            return AZ::Failure(AZStd::string::format("The target slot with id %s is not a part of this node %s, a connection cannot be made", sourceEndpoint.GetSlotId().ToString().data(), sourceEndpoint.GetNodeId().ToString().data()));
        }

        auto targetNode = FindNode(targetEndpoint.GetNodeId());
        if (targetNode == nullptr)
        {
            return AZ::Failure(AZStd::string::format("The target node with id %s is not a part of this graph, a connection cannot be made", targetEndpoint.GetNodeId().ToString().data()));
        }

        Slot* targetSlot = targetNode->GetSlot(targetEndpoint.GetSlotId());

        if (targetSlot == nullptr)
        {
            return AZ::Failure(AZStd::string::format("The target slot with id %s is not a part of this node %s, a connection cannot be made", sourceEndpoint.GetSlotId().ToString().data(), sourceEndpoint.GetNodeId().ToString().data()));
        }

        auto outcome = Connection::ValidateConnection((*sourceSlot), (*targetSlot));
        return outcome;
    }

    bool Graph::Disconnect(const AZ::EntityId& sourceNodeId, const SlotId& sourceSlotId, const AZ::EntityId& targetNodeId, const SlotId& targetSlotId)
    {
        return DisconnectByEndpoint({ sourceNodeId, sourceSlotId }, { targetNodeId, targetSlotId });
    }

    bool Graph::DisconnectByEndpoint(const Endpoint& sourceEndpoint, const Endpoint& targetEndpoint)
    {
        AZ::Entity* connectionEntity{};
        if (FindConnection(connectionEntity, sourceEndpoint, targetEndpoint) && RemoveConnection(connectionEntity->GetId()))
        {
            delete connectionEntity;
            return true;
        }
        return false;
    }

    bool Graph::DisconnectById(const AZ::EntityId& connectionId)
    {
        if (RemoveConnection(connectionId))
        {
            AZ::ComponentApplicationBus::Broadcast(&AZ::ComponentApplicationRequests::DeleteEntity, connectionId);
            return true;
        }

        return false;
    }

    void Graph::RefreshConnectionValidity(bool warnOnRemoval)
    {
        AZStd::vector<AZ::EntityId> removableConnections;

        for (auto& connectionEntity : m_graphData.m_connections)
        {
            auto outcome = ValidateConnection(connectionEntity);

            if (!outcome.IsSuccess())
            {
                AZ_Warning("ScriptCanvas", !warnOnRemoval, outcome.GetError().m_errorDescription.data());
                removableConnections.emplace_back(connectionEntity->GetId());
            }
        }

        for (auto connectionId : removableConnections)
        {
            DisconnectById(connectionId);
        }

        if (!removableConnections.empty())
        {
            RefreshConnectionValidity(warnOnRemoval);
        }
    }
  
    void Graph::OnEntityActivated(const AZ::EntityId&)
    {
        AZ::EntityBus::Handler::BusDisconnect(GetEntityId());
        m_executionContext.Execute();
    }

    bool Graph::AddGraphData(const GraphData& graphData)
    {
        bool success = true;

        m_batchAddingData = true;
        GraphNotificationBus::Event(GetScriptCanvasId(), &GraphNotifications::OnBatchAddBegin);

        for (auto&& nodeItem : graphData.m_nodes)
        {
            success = AddItem(nodeItem) && success;
        }

        for (auto&& nodeItem : graphData.m_connections)
        {
            success = AddItem(nodeItem) && success;
        }

        for (auto&& nodeItem : graphData.m_nodes)
        {
            if (auto scriptEventNode = AZ::EntityUtils::FindFirstDerivedComponent<Nodes::Core::Internal::ScriptEventBase>(nodeItem))
            {
                AZ::Data::AssetManager::Instance().GetAsset<ScriptEvents::ScriptEventsAsset>(scriptEventNode->GetAssetId(), false);
            }
        }
        
        m_batchAddingData = false;
        GraphNotificationBus::Event(GetScriptCanvasId(), &GraphNotifications::OnBatchAddComplete);

        return success;
    }

    void Graph::RemoveGraphData(const GraphData& graphData)
    {
        RemoveItems(graphData.m_connections);
        RemoveItems(graphData.m_nodes);
    }

    bool Graph::IsBatchAddingGraphData() const
    {
        return m_batchAddingData;
    }

    AZStd::unordered_set<AZ::Entity*> Graph::CopyItems(const AZStd::unordered_set<AZ::Entity*>& entities)
    {
        AZStd::unordered_set<AZ::Entity*> elementsToCopy;
        for (const auto& nodeElement : m_graphData.m_nodes)
        {
            if (entities.find(nodeElement) != entities.end())
            {
                elementsToCopy.emplace(nodeElement);
            }
        }

        for (const auto& connectionElement : m_graphData.m_connections)
        {
            if (entities.find(connectionElement) != entities.end())
            {
                elementsToCopy.emplace(connectionElement);
            }
        }

        return elementsToCopy;
    }

    void Graph::AddItems(const AZStd::unordered_set<AZ::Entity*>& graphField)
    {
        for (auto& graphElementRef : graphField)
        {
            AddItem(graphElementRef);
        }
    }

    void Graph::RemoveItems(const AZStd::unordered_set<AZ::Entity*>& graphField)
    {
        for (auto& graphElementRef : graphField)
        {
            RemoveItem(graphElementRef);
        }
    }

    void Graph::RemoveItems(const AZStd::vector<AZ::Entity*>& graphField)
    {
        for (auto& graphElementRef : graphField)
        {
            RemoveItem(graphElementRef);
        }
    }

    bool Graph::ValidateConnectionEndpoints(const AZ::EntityId& connectionRef, const AZStd::unordered_set<AZ::EntityId>& nodeRefs)
    {
        AZ::Entity* entity{};
        AZ::ComponentApplicationBus::BroadcastResult(entity, &AZ::ComponentApplicationRequests::FindEntity, connectionRef);
        auto* connection = entity ? AZ::EntityUtils::FindFirstDerivedComponent<Connection>(entity) : nullptr;
        if (connection)
        {
            auto sourceIt = nodeRefs.find(connection->GetSourceNode());
            auto targetIt = nodeRefs.find(connection->GetTargetNode());
            return sourceIt != nodeRefs.end() && targetIt != nodeRefs.end();
        }

        return false;
    }

    AZStd::unordered_set<AZ::Entity*> Graph::GetItems() const
    {
        AZStd::unordered_set<AZ::Entity*> result;

        for (auto& nodeEntity : m_graphData.m_nodes)
        {
            if (nodeEntity)
            {
                result.emplace(nodeEntity);
            }
        }

        for (auto& connectionEntity : m_graphData.m_connections)
        {
            if (connectionEntity)
            {
                result.emplace(connectionEntity);
            }
        }
        return result;
    }

    VariableData* Graph::GetVariableData()
    {
        return m_variableRequests->GetVariableData();
    }

    const GraphVariableMapping* Graph::GetVariables() const
    {
        return m_variableRequests->GetVariables();
    }

    GraphVariable* Graph::FindVariable(AZStd::string_view propName)
    {
        return m_variableRequests->FindVariable(propName);
    }

    GraphVariable* Graph::FindVariableById(const VariableId& variableId)
    {
        return m_variableRequests->FindVariableById(variableId);
    }

    Data::Type Graph::GetVariableType(const VariableId& variableId) const
    {
        return m_variableRequests->GetVariableType(variableId);
    }

    AZStd::string_view Graph::GetVariableName(const VariableId& variableId) const
    {
        return m_variableRequests->GetVariableName(variableId);
    }

    bool Graph::IsGraphObserved() const
    {
        return m_isObserved;
    }

    void Graph::SetIsGraphObserved(bool isObserved)
    {
        m_isObserved = isObserved;
    }
}
