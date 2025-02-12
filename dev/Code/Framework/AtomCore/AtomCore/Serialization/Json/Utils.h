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

#include <AzCore/Serialization/ObjectStream.h>
#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/std/string/string.h>
#include <AzCore/IO/FileIO.h>
#include <AzCore/JSON/document.h>
#include <AzCore/Serialization/Json/JsonSerializationResult.h>

namespace AZ
{
    namespace IO
    {
        class GenericStream;
    }

    struct JsonSerializerSettings;
    struct JsonDeserializerSettings;

    // Utility functions which use json serializer/deserializer to save/load object to file/stream 
    namespace JsonSerializationUtils
    {
        ///////////////////////////////////////////////////////////////////////////////////
        // Save functions

        AZ::Outcome<void, AZStd::string> SaveObjectToStreamByType(const void* objectPtr, const Uuid& objectType, IO::GenericStream& stream,
            const void* defaultObjectPtr = nullptr, const JsonSerializerSettings* settings = nullptr);
        AZ::Outcome<void, AZStd::string> SaveObjectToFileByType(const void* objectPtr, const Uuid& objectType, const AZStd::string& filePath,
            const void* defaultObjectPtr = nullptr, const JsonSerializerSettings* settings = nullptr);

        template <typename ObjectType>
        AZ::Outcome<void, AZStd::string> SaveObjectToStream(const ObjectType* classPtr, IO::GenericStream& stream,
            const ObjectType* defaultClassPtr = nullptr, const JsonSerializerSettings* settings = nullptr)
        {
            return SaveObjectToStreamByType(classPtr, AzTypeInfo<ObjectType>::Uuid(), stream, defaultClassPtr, settings);
        }

        template <typename ObjectType>
        AZ::Outcome<void, AZStd::string> SaveObjectToFile(const ObjectType* classPtr, const AZStd::string& filePath,
            const ObjectType* defaultClassPtr = nullptr, const JsonSerializerSettings* settings = nullptr)
        {
            return SaveObjectToFileByType(classPtr, AzTypeInfo<ObjectType>::Uuid(), filePath, defaultClassPtr, settings);
        }

        ///////////////////////////////////////////////////////////////////////////////////
        // Load functions

        //! Parse json text. Returns a failure with error message if the content is not valid JSON.
        AZ::Outcome<rapidjson::Document, AZStd::string> ParseJson(AZStd::string_view jsonText);

        //! Parse a json file. Returns a failure with error message if the content is not valid JSON.
        AZ::Outcome<rapidjson::Document, AZStd::string> LoadJson(AZStd::string_view filePath);

        //! Parse a json stream. Returns a failure with error message if the content is not valid JSON.
        AZ::Outcome<rapidjson::Document, AZStd::string> LoadJson(IO::GenericStream& stream);
        
        //! Load object with known class type
        AZ::Outcome<void, AZStd::string> LoadObjectFromStreamByType(void* objectToLoad, const Uuid& objectType, IO::GenericStream& stream,
            const JsonDeserializerSettings* settings = nullptr);
        
        template <typename ObjectType>
        AZ::Outcome<void, AZStd::string> LoadObjectFromStream(ObjectType& objectToLoad, IO::GenericStream& stream, const JsonDeserializerSettings* settings = nullptr)
        {
            return LoadObjectFromStreamByType(&objectToLoad, AzTypeInfo<ObjectType>::Uuid(), stream, settings);
        }

        template <typename ObjectType>
        AZ::Outcome<void, AZStd::string> LoadObjectFromFile(ObjectType& objectToLoad, const AZStd::string& filePath, const JsonDeserializerSettings* settings = nullptr)
        {
            AZ::IO::FileIOStream inputFileStream;
            if (!inputFileStream.Open(filePath.c_str(), AZ::IO::OpenMode::ModeRead | AZ::IO::OpenMode::ModeText))
            {
                return AZ::Failure(AZStd::string::format("Error opening file '%s' for reading", filePath.c_str()));
            }
            return LoadObjectFromStream(objectToLoad, inputFileStream, settings);
        }

        //! Load any object
        AZ::Outcome<AZStd::any, AZStd::string> LoadAnyObjectFromStream(IO::GenericStream& stream, const JsonDeserializerSettings* settings = nullptr);
        AZ::Outcome<AZStd::any, AZStd::string> LoadAnyObjectFromFile(const AZStd::string& filePath,const JsonDeserializerSettings* settings = nullptr);

        ///////////////////////////////////////////////////////////////////////////////////
        // Reporting functions

        //! Reporting callback that can be used in JsonSerializerSettings to report AZ_Waring when fields are Skipped or Unsupported or processing is not Completed.
        AZ::JsonSerializationResult::ResultCode ReportCommonWarnings(AZStd::string_view message, AZ::JsonSerializationResult::ResultCode result, AZStd::string_view path);

    } // namespace JsonSerializationUtils
} // namespace Az
