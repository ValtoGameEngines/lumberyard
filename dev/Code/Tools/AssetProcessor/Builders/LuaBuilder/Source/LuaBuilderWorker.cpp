/*
 * All or portions of this file Copyright(c) Amazon.com, Inc.or its affiliates or
 * its licensors.
 *
 * For complete copyright and license terms please see the LICENSE at the root of this
 * distribution (the "License"). All use of this software is governed by the License,
 *or, if provided, by the license below or the license accompanying this file. Do not
 * remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
 *WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *
 */

#include "LuaBuilderWorker.h"
#include "LuaHelpers.h"

#include <AzCore/Casting/numeric_cast.h>
#include <AzCore/Debug/Trace.h>
#include <AzCore/Math/MathReflection.h>
#include <AzCore/Script/ScriptAsset.h>
#include <AzCore/Script/ScriptContext.h>
#include <AzCore/Script/lua/lua.h>

#include <AzFramework/StringFunc/StringFunc.h>
#include <AzFramework/IO/LocalFileIO.h>

#include <AzCore/std/string/conversions.h>
#include <AzFramework/FileFunc/FileFunc.h>

namespace LuaBuilder
{
    namespace
    {
        //////////////////////////////////////////////////////////////////////////
        // Helper for writing to a generic stream
        template<typename T>
        bool WriteToStream(AZ::IO::GenericStream& stream, const T* t)
        {
            return stream.Write(sizeof(T), t) == sizeof(T);
        }

        static const AZ::u32 s_BuildTypeKey  = AZ_CRC("BuildType", 0xd01cbdd7);
        static const char* s_BuildTypeCompiled = "Compiled";
        static const char* s_BuildTypeText     = "Text";
    }

    //////////////////////////////////////////////////////////////////////////
    // CreateJobs
    void LuaBuilderWorker::CreateJobs(const AssetBuilderSDK::CreateJobsRequest& request, AssetBuilderSDK::CreateJobsResponse& response)
    {
        using namespace AssetBuilderSDK;

        // Check for shutdown
        if (m_isShuttingDown)
        {
            response.m_result = CreateJobsResultCode::ShuttingDown;
            return;
        }
        
        for (const AssetBuilderSDK::PlatformInfo& info : request.m_enabledPlatforms)
        {
            JobDescriptor descriptor;
            descriptor.m_jobKey = "Lua Compile";
            descriptor.SetPlatformIdentifier(info.m_identifier.c_str());
            descriptor.m_critical = true;
            descriptor.m_jobParameters[s_BuildTypeKey] = info.HasTag("android") ? s_BuildTypeText : s_BuildTypeCompiled;
            response.m_createJobOutputs.push_back(descriptor);
        }

        response.m_result = CreateJobsResultCode::Success;
    }

    //////////////////////////////////////////////////////////////////////////
    // ProcessJob
    void LuaBuilderWorker::ProcessJob(const AssetBuilderSDK::ProcessJobRequest& request, AssetBuilderSDK::ProcessJobResponse& response)
    {
        AZ_TracePrintf(AssetBuilderSDK::InfoWindow, "Starting Job.\n");

        // We succeed unless I say otherwise.
        response.m_resultCode = AssetBuilderSDK::ProcessJobResult_Success;

        // Check for shutdown
        if (m_isShuttingDown)
        {
            AZ_TracePrintf(AssetBuilderSDK::InfoWindow, "Cancelled job %s because shutdown was requested.\n", request.m_sourceFile.c_str());
            response.m_resultCode = AssetBuilderSDK::ProcessJobResult_Cancelled;
            return;
        }

        JobStepOutcome result = AZ::Failure(AssetBuilderSDK::ProcessJobResult_Failed);
        // Run compile when compile type, text when text type
        if (request.m_jobDescription.m_jobParameters.at(s_BuildTypeKey) == s_BuildTypeCompiled)
        {
            // Run compile
            result = RunCompileJob(request);
        }
        else if (request.m_jobDescription.m_jobParameters.at(s_BuildTypeKey) == s_BuildTypeText)
        {
            // Run compile
            result = RunCopyJob(request);
        }

        AssetBuilderSDK::ProductPathDependencySet dependencySet;

        if (result.IsSuccess())
        {
            response.m_outputProducts.emplace_back(result.TakeValue());

            ParseDependencies(request.m_fullPath, dependencySet);
            response.m_outputProducts.back().m_pathDependencies.insert(dependencySet.begin(), dependencySet.end());
        }
        else
        {
            response.m_resultCode = result.GetError();
            return;
        }

        // Run copy
        response.m_outputProducts.emplace_back(request.m_fullPath, azrtti_typeid<AZ::ScriptAsset>(), AZ::ScriptAsset::CopiedAssetSubId);
        response.m_outputProducts.back().m_pathDependencies.swap(dependencySet);
    }

    //////////////////////////////////////////////////////////////////////////
    // ShutDown
    void LuaBuilderWorker::ShutDown()
    {
        // it is important to note that this will be called on a different thread than your process job thread
        m_isShuttingDown = true;
    }

// Ensures condition is true, otherwise fails the build job.
#define LB_VERIFY(condition, ...)                                               \
        if (!(condition))                                                       \
        {                                                                       \
            AZ_Error(AssetBuilderSDK::ErrorWindow, false, __VA_ARGS__);         \
            return AZ::Failure(AssetBuilderSDK::ProcessJobResult_Failed);       \
        }

    //////////////////////////////////////////////////////////////////////////
    // RunCompileJob
    LuaBuilderWorker::JobStepOutcome LuaBuilderWorker::RunCompileJob(const AssetBuilderSDK::ProcessJobRequest& request)
    {
        AZ_TracePrintf(AssetBuilderSDK::InfoWindow, "Starting script compile.\n");

        // Setup lua state
        AZ::ScriptContext scriptContext(AZ::DefaultScriptContextId);

        // Reset filename to .luac, reconstruct full path
        AZStd::string destFileName;
        AzFramework::StringFunc::Path::GetFullFileName(request.m_fullPath.c_str(), destFileName);
        AzFramework::StringFunc::Path::ReplaceExtension(destFileName, "luac");

        AZStd::string debugName = "@" + request.m_sourceFile;
        AZStd::to_lower(debugName.begin(), debugName.end());

        using namespace AZ::IO;

        // Read script
        {
            FileIOStream inputStream;
            LB_VERIFY(inputStream.Open(request.m_fullPath.c_str(), OpenMode::ModeRead | OpenMode::ModeText), "Failed to open input file %s", request.m_sourceFile.c_str());

            // Parse asset
            LB_VERIFY(scriptContext.LoadFromStream(&inputStream, debugName.c_str()),
                "%s", lua_tostring(scriptContext.NativeContext(), -1));

            inputStream.Seek(0, AZ::IO::GenericStream::SeekMode::ST_SEEK_BEGIN);
        }

        // Write result
        // Asset format:
        // u8: asset version
        // u8: asset type (compiled)
        // void*: Script data
        {
            AZStd::string destPath;
            AzFramework::StringFunc::Path::ConstructFull(request.m_tempDirPath.c_str(), destFileName.c_str(), destPath, true);

            FileIOStream outputStream;
            LB_VERIFY(outputStream.Open(destPath.c_str(), OpenMode::ModeWrite | OpenMode::ModeBinary), "Failed to open output file %s", destPath.c_str());

            AZ_TracePrintf(AssetBuilderSDK::InfoWindow, "Beginning writing of metadata.\n");

            // Write asset version
            AZ::ScriptAsset::LuaScriptInfo currentField = AZ::ScriptAsset::AssetVersion;
            LB_VERIFY(WriteToStream(outputStream, &currentField), "Failed writing asset version to stream.");
            // Write asset type
            currentField = AZ::ScriptAsset::AssetTypeCompiled;
            LB_VERIFY(WriteToStream(outputStream, &currentField), "Failed to write asset type to stream.");

            AZ_TracePrintf(AssetBuilderSDK::InfoWindow, "Beginning writing of script data.\n");

            // Write script
            LB_VERIFY(LuaDumpToStream(outputStream, scriptContext.NativeContext()), "Failed to write lua script to stream.");
        }

        return AZ::Success(AssetBuilderSDK::JobProduct{ destFileName, azrtti_typeid<AZ::ScriptAsset>(), AZ::ScriptAsset::CompiledAssetSubId });
    }

    //////////////////////////////////////////////////////////////////////////
    // RunCompileJob
    LuaBuilderWorker::JobStepOutcome LuaBuilderWorker::RunCopyJob(const AssetBuilderSDK::ProcessJobRequest& request)
    {
        AZ_TracePrintf(AssetBuilderSDK::InfoWindow, "Starting script compile.\n");

        // Setup lua state
        AZ::ScriptContext scriptContext(AZ::DefaultScriptContextId);

        // Reset filename to .luac, reconstruct full path
        AZStd::string destFileName;
        AzFramework::StringFunc::Path::GetFullFileName(request.m_fullPath.c_str(), destFileName);
        AzFramework::StringFunc::Path::ReplaceExtension(destFileName, "luac");

        AZStd::string debugName = "@" + request.m_sourceFile;
        AZStd::to_lower(debugName.begin(), debugName.end());

        AZStd::string sourceContents;

        using namespace AZ::IO;

        // Read script
        {
            FileIOStream inputStream;
            LB_VERIFY(inputStream.Open(request.m_fullPath.c_str(), OpenMode::ModeRead | OpenMode::ModeText), "Failed to open input file %s", request.m_sourceFile.c_str());

            // Read asset into string
            sourceContents.resize_no_construct(inputStream.GetLength());
            LB_VERIFY(inputStream.Read(sourceContents.size(), sourceContents.data()), "Failed to read script text.");
        }

        // Parse the script, ensure it's correctness
        {
            MemoryStream sourceStream(sourceContents.data(), sourceContents.size());

            // Parse asset
            LB_VERIFY(scriptContext.LoadFromStream(&sourceStream, debugName.c_str()),
                "%s", lua_tostring(scriptContext.NativeContext(), -1));
        }

        // Write result
        // Asset format:
        // u8: asset version
        // u8: asset type (text)
        // u32: debug name length
        // str[len]: debug name
        // void*: Script data
        {
            AZStd::string destPath;
            AzFramework::StringFunc::Path::ConstructFull(request.m_tempDirPath.c_str(), destFileName.c_str(), destPath, true);

            FileIOStream outputStream;
            LB_VERIFY(outputStream.Open(destPath.c_str(), OpenMode::ModeWrite | OpenMode::ModeBinary), "Failed to open output file %s", destPath.c_str());

            AZ_TracePrintf(AssetBuilderSDK::InfoWindow, "Beginning writing of metadata.\n");

            // Write asset version
            AZ::ScriptAsset::LuaScriptInfo currentField = AZ::ScriptAsset::AssetVersion;
            LB_VERIFY(WriteToStream(outputStream, &currentField), "Failed writing asset version to stream.");
            // Write asset type
            currentField = AZ::ScriptAsset::AssetTypeText;
            LB_VERIFY(WriteToStream(outputStream, &currentField), "Failed to write asset type to stream.");

            // Write the length of the debug name
            AZ::u32 debugNameLength = aznumeric_cast<AZ::u32>(debugName.size());
            LB_VERIFY(WriteToStream(outputStream, &debugNameLength), "Failed to write debug name length to stream.");

            // Write the debug name
            LB_VERIFY(outputStream.Write(debugName.size(), debugName.data()) == debugNameLength, "Failed to write debug name to stream.");

            AZ_TracePrintf(AssetBuilderSDK::InfoWindow, "Beginning writing of script data.\n");

            LB_VERIFY(outputStream.Write(sourceContents.size(), sourceContents.data()) == sourceContents.size(), "Failed to write lua script");
        }

        return AZ::Success(AssetBuilderSDK::JobProduct{ destFileName, azrtti_typeid<AZ::ScriptAsset>(), AZ::ScriptAsset::CompiledAssetSubId });
    }

    void LuaBuilderWorker::ParseDependencies(const AZStd::string& file, AssetBuilderSDK::ProductPathDependencySet& outDependencies)
    {
        AzFramework::FileFunc::ReadTextFileByLine(file, [&outDependencies](const char* line) -> bool
        {
            // Regex to match lines looking similar to require("a") or Script.ReloadScript("a") or require "a"
            // Group 1: require or empty
            // Group 2: quotation mark ("), apostrophe ('), or empty
            // Group 3: specified path or variable (variable will be indicated by empty group 2)
            // Group 4: Same as group 2
            AZStd::regex requireRegex(R"(\b(?:(require)|Script\.ReloadScript)\s*[\( ]\s*("|'|)([^"')]*)("|'|)\s*\)?)");

            // Regex to match lines looking like a path (containing a /)
            // Group 1: the string contents
            AZStd::regex pathRegex(R"~("((?=[^"]*\/)[^"\n<>:"|?*]{2,})")~");

            // Regex to match lines looking like ExecuteConsoleCommand("exec somefile.cfg")
            AZStd::regex consoleCommandRegex(R"~(ExecuteConsoleCommand\("exec (.*)"\))~");

            AZStd::smatch match;
            if (AZStd::regex_search(line, match, requireRegex))
            {
                if (!match[2].matched || !match[4].matched)
                {
                    // Result is not a string literal, we'll have to rely on the path regex to pick up the dependency
                }
                else
                {
                    AZStd::string filePath = match[3].str();

                    if (match[1].matched)
                    {
                        // This is a "require" include, which has a format that uses . instead of / and has no file extension included
                        static constexpr char s_luaExtension[] = ".luac";

                        // Replace '.' in module name with '/'
                        for (auto pos = filePath.find('.'); pos != AZStd::string::npos; pos = filePath.find('.'))
                        {
                            filePath.replace(pos, 1, "/", 1);
                        }

                        // Add file extension to path
                        if (filePath.find(s_luaExtension) == AZStd::string::npos)
                        {
                            filePath += s_luaExtension;
                        }
                    }

                    outDependencies.emplace(filePath, AssetBuilderSDK::ProductPathDependencyType::ProductFile);
                }
            }
            else if (AZStd::regex_search(line, match, consoleCommandRegex))
            {
                outDependencies.emplace(match[1].str().c_str(), AssetBuilderSDK::ProductPathDependencyType::ProductFile);
            }
            else if(AZStd::regex_search(line, match, pathRegex))
            {
                AZ_TracePrintf("LuaBuilder", "Found potential dependency on file: %s\n", match[1].str().c_str());

                outDependencies.emplace(match[1].str().c_str(), AssetBuilderSDK::ProductPathDependencyType::ProductFile);
            }

            return true;
        });
    }

#undef LB_VERIFY
}
