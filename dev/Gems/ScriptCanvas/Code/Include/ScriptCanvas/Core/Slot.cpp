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

#include "Slot.h"
#include "SlotMetadata.h"
#include "Graph.h"
#include "Node.h"
#include "Contracts.h"

#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Serialization/Utils.h>

#include <ScriptCanvas/Core/Contracts/ExclusivePureDataContract.h>
#include <ScriptCanvas/Variable/VariableBus.h>

#include <ScriptCanvas/Utils/DataUtils.h>

namespace ScriptCanvas
{
    /////////
    // Slot
    /////////

    static bool SlotVersionConverter(AZ::SerializeContext& context, AZ::SerializeContext::DataElementNode& classElement)
    {
        // SlotName
        if (classElement.GetVersion() <= 6)
        {
            auto slotNameElements = AZ::Utils::FindDescendantElements(context, classElement, AZStd::vector<AZ::Crc32>{AZ_CRC("id", 0xbf396750), AZ_CRC("m_name", 0xc08c4427)});
            AZStd::string slotName;
            if (slotNameElements.empty() || !slotNameElements.front()->GetData(slotName))
            {
                return false;
            }

            classElement.AddElementWithData(context, "slotName", slotName);
        }

        // Index fields
        if (classElement.GetVersion() <= 8)
        {
            classElement.RemoveElementByName(AZ_CRC("index", 0x80736701));
        }

        // Dynamic Type Fields
        if (classElement.GetVersion() <= 9)
        {
            classElement.AddElementWithData(context, "DynamicTypeOverride", DynamicDataType::None);
        }
        else if (classElement.GetVersion() < 11)
        {
            AZ::SerializeContext::DataElementNode* subElement = classElement.FindSubElement(AZ_CRC("dataTypeOverride", 0x7f1765d9));

            int enumValue = 0;
            if (subElement->GetData(enumValue))
            {
                if (enumValue != 0)
                {
                    classElement.AddElementWithData(context, "DynamicTypeOverride", DynamicDataType::Container);
                }
                else
                {
                    classElement.AddElementWithData(context, "DynamicTypeOverride", DynamicDataType::None);
                }
            }

            classElement.RemoveElementByName(AZ_CRC("dataTypeOverride", 0x7f1765d9));
        }
        
        // DisplayDataType
        if (classElement.GetVersion() < 12)
        {
            classElement.AddElementWithData(context, "DisplayDataType", Data::Type::Invalid());
        }

        // Descriptor
        if (classElement.GetVersion() <= 13)
        {
            AZ::SerializeContext::DataElementNode* subElement = classElement.FindSubElement(AZ_CRC("type", 0x8cde5729));

            int enumValue = 0;
            if (subElement && subElement->GetData(enumValue))
            {
                CombinedSlotType combinedSlotType = static_cast<CombinedSlotType>(enumValue);

                SlotDescriptor slotDescriptor = SlotDescriptor(combinedSlotType);

                classElement.AddElementWithData(context, "Descriptor", slotDescriptor);

                bool isLatent = (combinedSlotType == CombinedSlotType::LatentOut);
                classElement.AddElementWithData(context, "IsLatent", isLatent);
            }

            classElement.RemoveElementByName(AZ_CRC("type", 0x8cde5729));
        }

        // DataType
        if (classElement.GetVersion() <= 15)
        {
            AZ::SerializeContext::DataElementNode* subElement = classElement.FindSubElement(AZ_CRC("Descriptor", 0x03927602));

            Slot::DataType dataType = Slot::DataType::NoData;

            SlotDescriptor slotDescriptor;
            if (subElement && subElement->GetData(slotDescriptor))
            {                
                if (slotDescriptor.IsData() && dataType == Slot::DataType::NoData)
                {                    
                    dataType = Slot::DataType::Data;
                }
            }

            classElement.AddElementWithData(context, "DataType", dataType);
        }
        // This data field wasn't actually being initalized correctly So need to re-version convert.
        else if (classElement.GetVersion() <= 17)
        {            
            AZ::SerializeContext::DataElementNode* subElement = classElement.FindSubElement(AZ_CRC("Descriptor", 0x03927602));

            Slot::DataType dataType = Slot::DataType::NoData;

            SlotDescriptor slotDescriptor;
            if (subElement && subElement->GetData(slotDescriptor))
            {
                if (slotDescriptor.IsData() && dataType == Slot::DataType::NoData)
                {
                    dataType = Slot::DataType::Data;
                }
            }

            classElement.RemoveElementByName(AZ_CRC("DataType", 0x8539af66));
            classElement.AddElementWithData(context, "DataType", dataType);
        }

        if (classElement.GetVersion() <= 17)
        {
            classElement.RemoveElementByName(AZ_CRC("nodeId", 0x9ce63325));
        }

        return true;
    }

    void Slot::Reflect(AZ::ReflectContext* reflection)
    {
        SlotId::Reflect(reflection);
        Contract::Reflect(reflection);
        RestrictedTypeContract::Reflect(reflection);
        DynamicTypeContract::Reflect(reflection);
        SlotTypeContract::Reflect(reflection);
        ConnectionLimitContract::Reflect(reflection);
        DisallowReentrantExecutionContract::Reflect(reflection);
        ContractRTTI::Reflect(reflection);
        IsReferenceTypeContract::Reflect(reflection);
        SlotMetadata::Reflect(reflection);
        SupportsMethodContract::Reflect(reflection);
        MathOperatorContract::Reflect(reflection);

        AZ::SerializeContext* serializeContext = azrtti_cast<AZ::SerializeContext*>(reflection);
        if (serializeContext)
        {
            serializeContext->Class<SlotDescriptor>()
                ->Version(1)
                ->Field("ConnectionType", &SlotDescriptor::m_connectionType)
                ->Field("SlotType", &SlotDescriptor::m_slotType)
            ;
            serializeContext->Class<Slot>()
                ->Version(18, &SlotVersionConverter)
                ->Field("id", &Slot::m_id)                
                ->Field("DynamicTypeOverride", &Slot::m_dynamicDataType)
                ->Field("contracts", &Slot::m_contracts)
                ->Field("slotName", &Slot::m_name)
                ->Field("toolTip", &Slot::m_toolTip)
                ->Field("DisplayDataType", &Slot::m_displayDataType)
                ->Field("DisplayGroup", &Slot::m_displayGroup)
                ->Field("Descriptor", &Slot::m_descriptor)
                ->Field("IsLatent", &Slot::m_isLatentSlot)
                ->Field("DynamicGroup", &Slot::m_dynamicGroup)
                ->Field("DataType", &Slot::m_dataType)
                ->Field("IsReference", &Slot::m_isVariableReference)
                ->Field("VariableReference", &Slot::m_variableReference)
                ;
        }

    }

    Slot::Slot(const SlotConfiguration& slotConfiguration)
        : m_name(slotConfiguration.m_name)
        , m_toolTip(slotConfiguration.m_toolTip)
        , m_isLatentSlot(slotConfiguration.m_isLatent)
        , m_descriptor(slotConfiguration.GetSlotDescriptor())
        , m_dynamicDataType(DynamicDataType::None)
        , m_id(slotConfiguration.m_slotId)       
    {
        if (!slotConfiguration.m_displayGroup.empty())
        {
            m_displayGroup = AZ::Crc32(slotConfiguration.m_displayGroup.c_str());
        }

        // Add the slot type contract by default, It is used for filtering input/output slots and flow/data slots
        m_contracts.emplace_back(AZStd::make_unique<SlotTypeContract>());

        // Every DataIn slot has a contract validating that only 1 connection from any PureData node is allowed
        if (IsData() && IsInput())
        {
            AddContract({ []() { return aznew ExclusivePureDataContract(); } });
        }

        for (const auto contractDesc : slotConfiguration.m_contractDescs)
        {
            AddContract(contractDesc);
        }

        if (const DataSlotConfiguration* dataSlotConfiguration = azrtti_cast<const DataSlotConfiguration*>(&slotConfiguration))
        {
            m_dataType = DataType::Data;
        }

        if (const DynamicDataSlotConfiguration* dynamicDataSlotConfiguration = azrtti_cast<const DynamicDataSlotConfiguration*>(&slotConfiguration))
        {
            m_dataType = DataType::Data;
            m_dynamicDataType = dynamicDataSlotConfiguration->m_dynamicDataType;
            m_dynamicGroup = dynamicDataSlotConfiguration->m_dynamicGroup;
        }
    }

    Slot::Slot(const Slot& other)
        : m_name(other.m_name)
        , m_toolTip(other.m_toolTip)
        , m_displayGroup(other.m_displayGroup)
        , m_dynamicGroup(other.m_dynamicGroup)
        , m_isLatentSlot(other.m_isLatentSlot)
        , m_descriptor(other.m_descriptor)
        , m_isVariableReference(other.m_isVariableReference)
        , m_dataType(other.m_dataType)
        , m_variableReference(other.m_variableReference)
        , m_dynamicDataType(other.m_dynamicDataType)
        , m_id(other.m_id)
        , m_node(other.m_node)
    {
        for (auto& otherContract : other.m_contracts)
        {
            m_contracts.emplace_back(AZ::EntityUtils::GetApplicationSerializeContext()->CloneObject(otherContract.get()));
        }

        SetDisplayType(other.m_displayDataType);
    }

    Slot::Slot(Slot&& slot)
        : m_name(AZStd::move(slot.m_name))
        , m_toolTip(AZStd::move(slot.m_toolTip))
        , m_displayGroup(AZStd::move(slot.m_displayGroup))
        , m_dynamicGroup(AZStd::move(slot.m_dynamicGroup))
        , m_isLatentSlot(AZStd::move(slot.m_isLatentSlot))
        , m_descriptor(AZStd::move(slot.m_descriptor))
        , m_isVariableReference(AZStd::move(slot.m_isVariableReference))
        , m_dataType(AZStd::move(slot.m_dataType))
        , m_variableReference(AZStd::move(slot.m_variableReference))
        , m_dynamicDataType(AZStd::move(slot.m_dynamicDataType))
        , m_displayDataType(AZStd::move(slot.m_displayDataType))
        , m_id(AZStd::move(slot.m_id))
        , m_node(AZStd::move(slot.m_node))
        , m_contracts(AZStd::move(slot.m_contracts))
    {
        SetDisplayType(slot.m_displayDataType);
    }

    Slot::~Slot()
    {
        VariableNotificationBus::Handler::BusDisconnect();
    }

    Slot& Slot::operator=(const Slot& slot)
    {
        m_name = slot.m_name;
        m_toolTip = slot.m_toolTip;
        m_displayGroup = slot.m_displayGroup;
        m_dynamicGroup = slot.m_dynamicGroup;
        m_isLatentSlot = slot.m_isLatentSlot;
        m_descriptor = slot.m_descriptor;
        m_isVariableReference = slot.m_isVariableReference;
        m_dataType = slot.m_dataType;
        m_variableReference = slot.m_variableReference;        
        m_dynamicDataType = slot.m_dynamicDataType;
        m_displayDataType = slot.m_displayDataType;
        m_id = slot.m_id;
        m_node = slot.m_node;        

        for (auto& otherContract : slot.m_contracts)
        {
            m_contracts.emplace_back(AZ::EntityUtils::GetApplicationSerializeContext()->CloneObject(otherContract.get()));
        }

        SetDisplayType(slot.m_displayDataType);

        return *this;
    }
    
    void Slot::AddContract(const ContractDescriptor& contractDesc)
    {
        if (contractDesc.m_createFunc)
        {
            Contract* newContract = contractDesc.m_createFunc();
            if (newContract)
            {
                m_contracts.emplace_back(newContract);
            }
        }
    }    

    void Slot::ConvertToLatentExecutionOut()
    {
        if (IsExecution() && IsOutput())
        {
            m_isLatentSlot = true;
        }
    }

    AZ::EntityId Slot::GetNodeId() const
    {
        return GetNode()->GetEntityId();
    }

    void Slot::SetNode(Node* node)
    {
        m_node = node;
    }

    void Slot::InitializeVariables()
    {        
        if (IsVariableReference())
        {
            m_variable = m_node->FindGraphVariable(m_variableReference);

            if (m_variable)
            {
                VariableNotificationBus::Handler::BusConnect(m_variable->GetGraphScopedId());

                if (IsInput())
                {
                    m_node->OnInputChanged((*m_variable->GetDatum()), GetId());
                }
            }
            else
            {
                SCRIPTCANVAS_REPORT_ERROR((*m_node), "Node (%s) is attempting to execute using an invalid Variable Reference", m_node->GetNodeName().c_str());
            }
        }        
    }

    Endpoint Slot::GetEndpoint() const
    { 
        return Endpoint(GetNode()->GetEntityId(), GetId());
    }

    Data::Type Slot::GetDataType() const
    {
        Data::Type retVal = m_node->GetSlotDataType(GetId());

        return retVal;
    }

    bool Slot::IsData() const
    {
        return m_descriptor.IsData();
    }

    const Datum* Slot::FindDatum() const
    {
        const Datum* datum = m_node->FindDatum(GetId());
        return datum;
    }

    void Slot::FindModifiableDatumView(ModifiableDatumView& datumView)
    {
        m_node->FindModifiableDatumView(GetId(), datumView);
    }

    bool Slot::IsVariableReference() const
    {
        return m_isVariableReference || m_dataType == DataType::VariableReference;
    }

    bool Slot::CanConvertToValue() const
    {
        return CanConvertTypes() && m_isVariableReference;
    }

    bool Slot::ConvertToValue()
    {
        if (CanConvertToValue())
        {
            m_isVariableReference = false;
            m_variableReference = ScriptCanvas::VariableId();

            if (m_node)
            {
                m_node->OnSlotConvertedToValue(GetId());
            }
        }

        return !m_isVariableReference;
    }

    bool Slot::CanConvertTypes() const
    {
        // Don't allow VariableId's to be variable references.
        return m_dataType == DataType::Data
            && GetDataType() != Data::Type::BehaviorContextObject(GraphScopedVariableId::TYPEINFO_Uuid());
    }

    bool Slot::CanConvertToReference() const
    {        
        return CanConvertTypes() && !m_isVariableReference && !m_node->HasConnectedNodes((*this));
    }

    bool Slot::ConvertToReference()
    {
        if (CanConvertToReference())
        {
            m_isVariableReference = true;            

            if (m_node)
            {
                m_node->OnSlotConvertedToReference(GetId());
            }
        }

        return m_isVariableReference;
    }

    void Slot::SetVariableReference(const VariableId& variableId)
    {
        if (!IsVariableReference() && !ConvertToReference())
        {
            return;
        }

        m_variableReference = variableId;

        if (IsDynamicSlot())
        {
            if (!HasDisplayType())
            {
                GraphVariable* variable = m_node->FindGraphVariable(m_variableReference);

                ScriptCanvas::Data::Type displayType = variable ? variable->GetDataType() : ScriptCanvas::Data::Type::Invalid();

                AZ::Crc32 dynamicGroup = GetDynamicGroup();

                if (dynamicGroup != AZ::Crc32())
                {
                    if (m_node->HasConcreteDisplayType(dynamicGroup))
                    {
                        m_node->SetDisplayType(dynamicGroup, displayType);
                    }
                }
                else
                {
                    SetDisplayType(displayType);
                }
            }
            else if (!m_variableReference.IsValid())
            {
                m_node->SanityCheckDynamicDisplay();
            }
        }

        NodeNotificationsBus::Event(m_node->GetEntityId(), &NodeNotifications::OnInputChanged, GetId());
    }

    const VariableId& Slot::GetVariableReference() const
    {
        return m_variableReference;
    }

    GraphVariable* Slot::GetVariable() const
    {
        return m_variable;
    }

    void Slot::ClearVariableReference()
    {
        SetVariableReference(VariableId());
    }

    bool Slot::IsExecution() const
    {
        return m_descriptor.IsExecution();
    }

    bool Slot::IsInput() const
    {
        return m_descriptor.IsInput();
    }

    bool Slot::IsOutput() const
    {
        return m_descriptor.IsOutput();
    }

    ScriptCanvas::ConnectionType Slot::GetConnectionType() const
    {
        return m_descriptor.m_connectionType;
    }

    bool Slot::IsLatent() const
    {
        return m_isLatentSlot;
    }

    void Slot::OnVariableValueChanged()
    {
        m_node->OnInputChanged((*m_variable->GetDatum()), GetId());
    }

    void Slot::SetDynamicDataType(DynamicDataType dynamicDataType)
    {
        AZ_Assert(m_dynamicDataType == DynamicDataType::None, "Set Dynamic Data Type is meant to be used for a node wise version conversion step. Not as a run time reconfiguration of a dynamic type.");

        if (m_dynamicDataType == DynamicDataType::None)
        {
            m_dynamicDataType = dynamicDataType;
        }
    }

    bool Slot::IsDynamicSlot() const
    {
        return m_dynamicDataType != DynamicDataType::None;
    }

    void Slot::SetDisplayType(ScriptCanvas::Data::Type displayType)
    {
        if ((m_displayDataType.IsValid() && !displayType.IsValid())
            || (!m_displayDataType.IsValid() && displayType.IsValid()))
        {
            // Confirm that the type we are display as conforms to what our underlying type says we
            // should be.
            if (displayType.IsValid() && IsDynamicSlot())
            {
                AZ::TypeId typeId = displayType.GetAZType();
                bool isContainerType = AZ::Utils::IsContainerType(typeId);

                if (m_dynamicDataType == DynamicDataType::Value && isContainerType)
                {
                    return;
                }
                else if (m_dynamicDataType == DynamicDataType::Container && !isContainerType)
                {
                    return;
                }
            }

            m_displayDataType = displayType;

            // For dynamic slots we want to manipulate the underlying data a little to simplify down the usages.
            // i.e. Just setting the display type of the slot should allow the datum to function as that type.
            //
            // For non-dynamic slots, I don't want to do anything since there might be some specialization
            // going on that I don't want to stomp on.
            if (IsDynamicSlot() && IsInput())
            {
                ModifiableDatumView datumView;
                GetNode()->ModifyUnderlyingSlotDatum(GetId(), datumView);

                if (datumView.IsValid())
                {
                    if (!datumView.IsType(m_displayDataType))
                    {
                        if (m_displayDataType.IsValid())
                        {
                            Datum sourceDatum(m_displayDataType, ScriptCanvas::Datum::eOriginality::Original);
                            sourceDatum.SetToDefaultValueOfType();

                            datumView.ReconfigureDatumTo(AZStd::move(sourceDatum));
                        }
                        else
                        {
                            datumView.ReconfigureDatumTo(AZStd::move(Datum()));
                        }
                    }
                }
            }

            if (m_node)
            {
                m_node->SignalSlotDisplayTypeChanged(m_id, GetDisplayType());
            }
        }
    }

    void Slot::ClearDisplayType()
    {
        if (IsDynamicSlot())
        {
            SetDisplayType(Data::Type::Invalid());
        }
    }

    ScriptCanvas::Data::Type Slot::GetDisplayType() const
    {
        return m_displayDataType;
    }    

    bool Slot::HasDisplayType() const
    {
        return m_displayDataType.IsValid();
    }

    AZ::Crc32 Slot::GetDisplayGroup() const
    {
        return m_displayGroup;
    }

    void Slot::SetDisplayGroup(AZStd::string displayGroup)
    {
        m_displayGroup = AZ::Crc32(displayGroup);
    }

    AZ::Crc32 Slot::GetDynamicGroup() const
    {
        return m_dynamicGroup;
    }

    AZ::Outcome<void, AZStd::string> Slot::IsTypeMatchFor(const Slot& otherSlot) const
    {
        AZ::Outcome<void, AZStd::string> matchForOutcome;

        ScriptCanvas::Data::Type myType = GetDataType();
        ScriptCanvas::Data::Type otherType = otherSlot.GetDataType();

        if (otherType.IsValid())
        {
            if (IsDynamicSlot() && GetDynamicGroup() != AZ::Crc32())
            {
                matchForOutcome = m_node->IsValidTypeForGroup(GetDynamicGroup(), otherType);

                if (!matchForOutcome.IsSuccess())
                {
                    return matchForOutcome;
                }
            }

            matchForOutcome = IsTypeMatchFor(otherType);

            if (!matchForOutcome)
            {
                return matchForOutcome;
            }
        }

        if (myType.IsValid())
        {
            if (otherSlot.IsDynamicSlot() && otherSlot.GetDynamicGroup() != AZ::Crc32())
            {
                matchForOutcome = otherSlot.m_node->IsValidTypeForGroup(otherSlot.GetDynamicGroup(), myType);

                if (!matchForOutcome)
                {
                    return matchForOutcome;
                }
            }

            matchForOutcome = otherSlot.IsTypeMatchFor(myType);

            if (!matchForOutcome)
            {
                return matchForOutcome;
            }
        }

        // Container check is either based on the concrete type associated with the slot.
        // Or the dynamic display type if no concrete type has been associated.
        bool isMyTypeContainer = AZ::Utils::IsContainerType(ScriptCanvas::Data::ToAZType(myType)) || (IsDynamicSlot() && !HasDisplayType() && GetDynamicDataType() == DynamicDataType::Container);
        bool isOtherTypeContainer = AZ::Utils::IsContainerType(ScriptCanvas::Data::ToAZType(otherType)) || (otherSlot.IsDynamicSlot() && !otherSlot.HasDisplayType() && otherSlot.GetDynamicDataType() == DynamicDataType::Container);

        // Confirm that our dynamic typing matches to the other. Or that hard types match the other in terms of dynamic slot types.
        if (IsDynamicSlot())
        {
            if (GetDynamicDataType() == DynamicDataType::Container && !isOtherTypeContainer)
            {
                if (otherSlot.HasDisplayType() || otherSlot.GetDynamicDataType() != DynamicDataType::Any)
                {
                    if (otherType.IsValid())
                    {
                        return AZ::Failure(AZStd::string::format("%s is not a valid Container type.", ScriptCanvas::Data::GetName(otherType)));
                    }
                    else
                    {
                        return AZ::Failure<AZStd::string>("Cannot connect Dynamic Container to Dynamic Value type.");
                    }
                }
            }
            else if (GetDynamicDataType() == DynamicDataType::Value && isOtherTypeContainer)
            {
                return AZ::Failure(AZStd::string::format("%s is a Container type and not a Value type.", ScriptCanvas::Data::GetName(otherType)));
            }
        }        

        if (otherSlot.IsDynamicSlot())
        {
            if (otherSlot.GetDynamicDataType() == DynamicDataType::Container && !isMyTypeContainer)
            {
                if (HasDisplayType() || GetDynamicDataType() != DynamicDataType::Any)
                {
                    if (myType.IsValid())
                    {
                        return AZ::Failure(AZStd::string::format("%s is not a valid Container type.", ScriptCanvas::Data::GetName(myType)));
                    }
                    else
                    {
                        return AZ::Failure<AZStd::string>("Cannot connect Dynamic Container to Dynamic Value type.");
                    }
                }
            }
            else if (otherSlot.GetDynamicDataType() == DynamicDataType::Value && isMyTypeContainer)
            {
                return AZ::Failure(AZStd::string::format("%s is a Container type and not a Value type.", ScriptCanvas::Data::GetName(myType)));
            }
        }

        // If either side is dynamic, and doesn't have a display type, we can stop checking here since we passed all the negative cases.
        // And we know that the hard type match will fail.
        if ((IsDynamicSlot() && !HasDisplayType())
            || (otherSlot.IsDynamicSlot() && !otherSlot.HasDisplayType()))
        {
            return AZ::Success();
        }

        // At this point we need to confirm the types are a match.
        if (myType.IS_A(otherType))
        {
            return AZ::Success();
        }
        
        return AZ::Failure(AZStd::string::format("%s is not a type match for %s", ScriptCanvas::Data::GetName(myType), ScriptCanvas::Data::GetName(otherType)));
    }

    AZ::Outcome<void, AZStd::string> Slot::IsTypeMatchFor(const ScriptCanvas::Data::Type& dataType) const
    {
        if (IsExecution())
        {
            return AZ::Failure<AZStd::string>("Execution slot cannot match Data types.");
        }

        AZ::Outcome<void, AZStd::string> failureReason;
        bool contractsAllowType = true;

        for (const auto& contract : m_contracts)
        {
            failureReason = contract->EvaluateForType(dataType);
            if (!failureReason)
            {
                return failureReason;
            }
        }

        if (GetDynamicDataType() == DynamicDataType::Any
            && !HasDisplayType())
        {
            return AZ::Success();
        }

        if (IsDynamicSlot())
        {
            auto outcomeResult = DataUtils::MatchesDynamicDataTypeOutcome(GetDynamicDataType(), dataType);

            if (!outcomeResult.IsSuccess())
            {
                return outcomeResult;
            }
            else if (!HasDisplayType())
            {
                return AZ::Success();
            }
        }

        // At this point we need to confirm the types are a match.
        if (GetDataType().IS_A(dataType))
        {
            return AZ::Success();
        }

        return AZ::Failure(AZStd::string::format("%s is not a type match for %s", ScriptCanvas::Data::GetName(GetDataType()), ScriptCanvas::Data::GetName(dataType)));
    }

    void Slot::Rename(AZStd::string_view newName)
    {
        if (m_name != newName)
        {
            m_name = newName;            

            NodeNotificationsBus::Event(m_node->GetEntityId(), &NodeNotifications::OnSlotRenamed, m_id, newName);
        }
    }

    void Slot::SetDynamicGroup(const AZ::Crc32& dynamicGroup)
    {
        m_dynamicGroup = dynamicGroup;
    }
}
