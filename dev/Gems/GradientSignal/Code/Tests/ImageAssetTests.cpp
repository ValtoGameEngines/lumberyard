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

#include "GradientSignal_precompiled.h"

#include <AzTest/AzTest.h>
#include <AzCore/Component/ComponentApplication.h>
#include <GradientSignal/GradientImageConversion.h>

#include <limits>

namespace
{
    namespace Detail
    {
        // Generate sequential input from 0 to I
        template <typename T, AZStd::size_t... I>
        auto GenerateInputImpl(AZStd::index_sequence<I...>, float scale)
        {
            return AZStd::array<T, sizeof...(I)>{aznumeric_cast<T>(scale * I)...};
        }

        // RGBA = 1, 2, 3, 4, ...
        template <typename T, AZStd::size_t Dimension, AZStd::size_t Channels>
        auto GenerateInput(float scale = 1.0f)
        {
            return GenerateInputImpl<T>(AZStd::make_index_sequence<Dimension * Dimension * Channels>{}, scale);
        }

        template <typename T, AZStd::size_t Len>
        auto SetupAssetAndConvert(const AZStd::array<T, Len>& data, AZ::u32 dimensions,
            ImageProcessing::EPixelFormat format, AZStd::size_t bytesPerPixel, const GradientSignal::ImageSettings& settings)
        {
            GradientSignal::ImageAsset asset;

            asset.m_imageWidth = dimensions;
            asset.m_imageHeight = dimensions;
            asset.m_bytesPerPixel = bytesPerPixel;
            asset.m_imageFormat = format;
            asset.m_imageData.resize(asset.m_bytesPerPixel * asset.m_imageWidth * asset.m_imageHeight);

            AZStd::copy(AZStd::cbegin(data), AZStd::cend(data),
                reinterpret_cast<T*>(asset.m_imageData.data()));

            return GradientSignal::ConvertImage(asset, settings);
        }

        template <typename T, AZStd::size_t Len, typename Op>
        void VerifyResult(const GradientSignal::ImageAsset& asset, const AZStd::array<T, Len>& expected, const Op& op)
        {
            AZ_Assert(Len == (asset.m_imageData.size() / sizeof(T)), "Size doesn't match!");

            auto assetData = reinterpret_cast<const T*>(asset.m_imageData.data());

            for (auto i : expected)
            {
                op(*assetData++, i);
            }
        }
    }

    class ImageAssetTest
        : public ::testing::Test
    {
    protected:
        AZ::ComponentApplication m_app;
        AZ::Entity* m_systemEntity = nullptr;

        void SetUp() override
        {
            AZ::ComponentApplication::Descriptor appDesc;
            appDesc.m_memoryBlocksByteSize = 128 * 1024 * 1024;
            m_systemEntity = m_app.Create(appDesc);
            m_app.AddEntity(m_systemEntity);
        }

        void TearDown() override
        {
            m_app.Destroy();
            m_systemEntity = nullptr;
        }
    };

    TEST_F(ImageAssetTest, GradientImageAssetConversionU8SingleScale)
    {
        // Converts a U8 buffer to another U8 buffer while scaling to cause overflow.
        using namespace GradientSignal;

        ImageSettings settings;
        settings.m_rgbTransform = ChannelExportTransform::MAX;
        settings.m_alphaTransform = AlphaExportTransform::MULTIPLY;
        settings.m_format = ExportFormat::U8;
        settings.m_useR = true;
        settings.m_useG = true;
        settings.m_useB = true;
        settings.m_useA = true;
        settings.m_autoScale = false;
        settings.m_scaleRangeMin = 100.0f;
        settings.m_scaleRangeMax = 255.0f;

        constexpr auto imageDimensions = 4;
        constexpr auto numChannels = 1;
        constexpr auto bytesPerPixel = numChannels * sizeof(AZ::u8);
        constexpr auto outputSize = imageDimensions * imageDimensions;
        constexpr auto scaling = 25;

        auto inputData = Detail::GenerateInput<AZ::u8, imageDimensions, numChannels>(scaling);

        auto asset = Detail::SetupAssetAndConvert(inputData, imageDimensions,
            ImageProcessing::EPixelFormat::ePixelFormat_R8, bytesPerPixel,
            settings);

        AZStd::array<AZ::u8, outputSize> expectedValues;

        for (AZ::u32 i = 0, index = 0; i < inputData.size(); i += numChannels)
        {
            auto in = aznumeric_cast<AZ::u8>(i * scaling);
            auto normalized = AZStd::clamp((in - settings.m_scaleRangeMin) / aznumeric_cast<double>(settings.m_scaleRangeMax - settings.m_scaleRangeMin), 0.0, 1.0);
            expectedValues[index++] = aznumeric_cast<AZ::u8>(AZ::Lerp(aznumeric_cast<double>(std::numeric_limits<AZ::u8>::lowest()),
                aznumeric_cast<double>(std::numeric_limits<AZ::u8>::max()), normalized));
        }

        Detail::VerifyResult(*asset, expectedValues, [](auto a, auto b)
        {
            EXPECT_EQ(a, b);
        });
    }

    TEST_F(ImageAssetTest, GradientImageAssetConversionF32F32Successful)
    {
        // Checks F32 to F32 conversion.
        using namespace GradientSignal;

        ImageSettings settings;
        settings.m_rgbTransform = ChannelExportTransform::MAX;
        settings.m_alphaTransform = AlphaExportTransform::MULTIPLY;
        settings.m_format = ExportFormat::F32;
        settings.m_useR = true;
        settings.m_useG = true;
        settings.m_useB = true;
        settings.m_useA = true;
        settings.m_autoScale = true;
        settings.m_scaleRangeMin = 0.0f;
        settings.m_scaleRangeMax = 255.0f;

        constexpr auto imageDimensions = 10;
        constexpr auto numChannels = 4;
        constexpr auto bytesPerPixel = numChannels * sizeof(float);
        constexpr auto outputSize = imageDimensions * imageDimensions;

        auto inputData = Detail::GenerateInput<float, imageDimensions, numChannels>();

        auto asset = Detail::SetupAssetAndConvert(inputData, imageDimensions,
            ImageProcessing::EPixelFormat::ePixelFormat_R32G32B32A32F, bytesPerPixel,
            settings);

        AZStd::array<float, outputSize> expectedValues;

        for (AZ::u32 i = 0, index = 0; i < inputData.size(); i += numChannels, ++index)
        {
            // RGB transform and A transform
            expectedValues[index] = aznumeric_cast<float>(AZStd::max(i, i + 1) * (i + 3));
        }

        auto [min, max] = AZStd::minmax_element(AZStd::cbegin(expectedValues), AZStd::cend(expectedValues));

        for (AZ::u32 i = 0, index = 0; i < inputData.size(); i += numChannels, ++index)
        {
            // Normalize
            expectedValues[index] = (expectedValues[index] - *min) / aznumeric_cast<float>(*max - *min);
        }
        
        Detail::VerifyResult(*asset, expectedValues, [](auto a, auto b)
        {
            EXPECT_NEAR(a, b, 0.01);
        });
    }

    TEST_F(ImageAssetTest, GradientImageAssetConversionU8U16Successful)
    {
        // Checks converting from U8 data to U16 data.
        using namespace GradientSignal;

        ImageSettings settings;
        settings.m_rgbTransform = ChannelExportTransform::MAX;
        settings.m_alphaTransform = AlphaExportTransform::MULTIPLY;
        settings.m_format = ExportFormat::U16;
        settings.m_useR = true;
        settings.m_useG = true;
        settings.m_useB = true;
        settings.m_useA = true;
        settings.m_autoScale = true;
        settings.m_scaleRangeMin = 0.0f;
        settings.m_scaleRangeMax = 255.0f;

        constexpr auto imageDimensions = 3;
        constexpr auto numChannels = 2;
        constexpr auto bytesPerPixel = numChannels * sizeof(AZ::u8);
        constexpr auto outputSize = imageDimensions * imageDimensions;

        auto inputData = Detail::GenerateInput<AZ::u8, imageDimensions, numChannels>();

        auto asset = Detail::SetupAssetAndConvert(inputData, imageDimensions,
            ImageProcessing::EPixelFormat::ePixelFormat_R8G8, bytesPerPixel,
            settings);

        // max(N, N + 1) = N + 1
        // 0 to 16 input start range -> 1 to 17 output
        // Result = (x - 1) / 16
        // x = N + 1
        // Result = N / 16
        // Transform result to u16 range -> Lerp
        AZStd::array<AZ::u16, outputSize> expectedValues;

        for (AZ::u32 i = 0, index = 0; i < inputData.size(); i += numChannels)
        {
            expectedValues[index++] = aznumeric_cast<AZ::u16>(AZ::Lerp(aznumeric_cast<double>(std::numeric_limits<AZ::u16>::lowest()),
                aznumeric_cast<double>(std::numeric_limits<AZ::u16>::max()), i / 16.0));
        }

        Detail::VerifyResult(*asset, expectedValues, [](auto a, auto b)
        {
            EXPECT_EQ(a, b);
        });
    }

    TEST_F(ImageAssetTest, GradientImageAssetConversionF32U8Successful)
    {
        // Checks converting from F32 data to U8 data.
        using namespace GradientSignal;

        ImageSettings settings;
        settings.m_rgbTransform = ChannelExportTransform::MAX;
        settings.m_alphaTransform = AlphaExportTransform::MULTIPLY;
        settings.m_format = ExportFormat::U8;
        settings.m_useR = true;
        settings.m_useG = true;
        settings.m_useB = true;
        settings.m_useA = true;
        settings.m_autoScale = true;
        settings.m_scaleRangeMin = 0.0f;
        settings.m_scaleRangeMax = 255.0f;

        constexpr auto imageDimensions = 3;
        constexpr auto numChannels = 1;
        constexpr auto bytesPerPixel = numChannels * sizeof(float);
        constexpr auto outputSize = imageDimensions * imageDimensions;

        auto inputData = Detail::GenerateInput<float, imageDimensions, numChannels>();

        auto asset = Detail::SetupAssetAndConvert(inputData, imageDimensions,
            ImageProcessing::EPixelFormat::ePixelFormat_R32F, bytesPerPixel,
            settings);

        // 0 - 8 range
        // min to max range down from float = no special normalization
        AZStd::array<AZ::u8, outputSize> expectedValues;

        for (AZ::u32 i = 0, index = 0; i < inputData.size(); i += numChannels)
        {
            expectedValues[index++] = aznumeric_cast<AZ::u8>(AZ::Lerp(aznumeric_cast<double>(std::numeric_limits<AZ::u8>::lowest()),
                aznumeric_cast<double>(std::numeric_limits<AZ::u8>::max()), i / 8.0));
        }

        Detail::VerifyResult(*asset, expectedValues, [](auto a, auto b)
        {
            EXPECT_EQ(a, b);
        });
    }

    TEST_F(ImageAssetTest, GradientImageAssetConversionNoBadState)
    {
        // Ensure no bad state is left due to converting from U16
        // to U32 and then back to U16.

        using namespace GradientSignal;

        ImageSettings settings;
        settings.m_rgbTransform = ChannelExportTransform::AVERAGE;
        settings.m_alphaTransform = AlphaExportTransform::MULTIPLY;
        settings.m_format = ExportFormat::U32;
        settings.m_useR = true;
        settings.m_useG = true;
        settings.m_useB = true;
        settings.m_useA = true;
        settings.m_autoScale = false;
        settings.m_scaleRangeMin = 0.0f;
        settings.m_scaleRangeMax = 1000.0f;

        constexpr auto imageDimensions = 4;
        constexpr auto numChannels = 4;
        constexpr auto bytesPerPixel = numChannels * sizeof(AZ::u16);
        constexpr auto outputSize = imageDimensions * imageDimensions;
        constexpr auto scaling = 100.0f;

        auto inputData = Detail::GenerateInput<AZ::u16, imageDimensions, numChannels>(scaling);

        auto asset = Detail::SetupAssetAndConvert(inputData, imageDimensions,
            ImageProcessing::EPixelFormat::ePixelFormat_R16G16B16A16, bytesPerPixel,
            settings);

        // Scaled = N * 100
        // Average = Scaled + 100
        // Result = Average * Normalized(Scaled + 300)
        // Normalized = X / 1000, where X equals Result
        // Input -> 0 - 60000
        // Finally scale across AZ::u32
        AZStd::array<AZ::u32, outputSize> expectedValues;

        for (AZ::u32 i = 0, index = 0; i < inputData.size(); i += numChannels)
        {
            auto curr = i * scaling;
            auto average = curr + scaling;
            auto alpha = aznumeric_cast<double>(curr + scaling * 3) / std::numeric_limits<AZ::u16>::max();
            auto result = aznumeric_cast<AZ::u16>(average * alpha);

            auto normal = result / aznumeric_cast<double>(settings.m_scaleRangeMax);
            normal = AZStd::clamp(normal, 0.0, 1.0);

            expectedValues[index++] = aznumeric_cast<AZ::u32>(AZ::Lerp(aznumeric_cast<double>(std::numeric_limits<AZ::u32>::lowest()),
                aznumeric_cast<double>(std::numeric_limits<AZ::u32>::max()), normal));
        }

        Detail::VerifyResult(*asset, expectedValues, [](auto a, auto b)
        {
            EXPECT_EQ(a, b);
        });

        settings.m_format = ExportFormat::U16;
        settings.m_autoScale = true;

        asset = Detail::SetupAssetAndConvert(expectedValues, imageDimensions,
            ImageProcessing::EPixelFormat::ePixelFormat_R32, sizeof(AZ::u32),
            settings);

        // Similar process as above.
        AZStd::array<AZ::u16, outputSize> expectedValues2;

        for (AZ::u32 i = 0; i < expectedValues.size(); ++i)
        {
            // The max value outputted from the previous operation.
            constexpr auto max = aznumeric_cast<double>(2516850834.0f);

            auto normal = expectedValues[i] / max;
            expectedValues2[i] = aznumeric_cast<AZ::u16>(AZ::Lerp(aznumeric_cast<double>(std::numeric_limits<AZ::u16>::lowest()),
                aznumeric_cast<double>(std::numeric_limits<AZ::u16>::max()), normal));
        }

        Detail::VerifyResult(*asset, expectedValues2, [](auto a, auto b)
        {
            EXPECT_EQ(a, b);
        });
    }

    TEST_F(ImageAssetTest, GradientImageAssetConversionBadScalingHandled)
    {
        // Checks handling of scaling in cases where min > max.

        using namespace GradientSignal;

        ImageSettings settings;
        settings.m_rgbTransform = ChannelExportTransform::AVERAGE;
        settings.m_alphaTransform = AlphaExportTransform::MULTIPLY;
        settings.m_format = ExportFormat::U32;
        settings.m_useR = true;
        settings.m_useG = true;
        settings.m_useB = true;
        settings.m_useA = true;
        settings.m_autoScale = false;
        settings.m_scaleRangeMin = 1000.0f;
        settings.m_scaleRangeMax = -200.0f;

        constexpr auto imageDimensions = 2;
        constexpr auto numChannels = 1;
        constexpr auto bytesPerPixel = numChannels * sizeof(float);
        constexpr auto outputSize = imageDimensions * imageDimensions;

        auto inputData = Detail::GenerateInput<float, imageDimensions, numChannels>(-100.0f);

        auto asset = Detail::SetupAssetAndConvert(inputData, imageDimensions,
            ImageProcessing::EPixelFormat::ePixelFormat_R32F, bytesPerPixel,
            settings);

        // min-max equal to 1000 -> all values get scaled 
        constexpr AZStd::array<AZ::u32, outputSize> expectedValues
        {
            std::numeric_limits<AZ::u32>::max(), std::numeric_limits<AZ::u32>::max(),
            std::numeric_limits<AZ::u32>::max(), std::numeric_limits<AZ::u32>::max()
        };

        Detail::VerifyResult(*asset, expectedValues, [](auto a, auto b)
        {
            EXPECT_EQ(a, b);
        });
    }

    TEST_F(ImageAssetTest, GradientImageAssetConversionEmptyImageHandled)
    {
        // Checks handling of an empty image.

        using namespace GradientSignal;

        ImageSettings settings;
        settings.m_rgbTransform = ChannelExportTransform::AVERAGE;
        settings.m_alphaTransform = AlphaExportTransform::MULTIPLY;
        settings.m_format = ExportFormat::U32;
        settings.m_useR = true;
        settings.m_useG = true;
        settings.m_useB = true;
        settings.m_useA = true;
        settings.m_autoScale = false;
        settings.m_scaleRangeMin = 1000.0f;
        settings.m_scaleRangeMax = -200.0f;

        constexpr auto imageDimensions = 0;
        constexpr auto numChannels = 0;
        constexpr auto bytesPerPixel = numChannels * sizeof(float);

        auto inputData = Detail::GenerateInput<float, imageDimensions, numChannels>();

        auto asset = Detail::SetupAssetAndConvert(inputData, imageDimensions,
            ImageProcessing::EPixelFormat::ePixelFormat_R32F, bytesPerPixel,
            settings);

        EXPECT_TRUE(asset->m_imageData.empty());
    }

    TEST_F(ImageAssetTest, GradientImageAssetConversionSameTypeSuccessful)
    {
        // Only a min max scale operation is applied to each type.

        using namespace GradientSignal;

        constexpr auto imageDimensions = std::integral_constant<AZStd::size_t, 3>{};
        constexpr auto outputSize = imageDimensions() * imageDimensions();

        // 9 increments from min type to max type, except for float, which is from 0 to 1.
        constexpr AZStd::array<AZ::u8, outputSize> goldenValues1
        {
            0, 31, 63,
            95, 127, 159,
            191, 223, 255
        };

        constexpr AZStd::array<AZ::u16, outputSize> goldenValues2
        {
            0, 8191, 16383,
            24575, 32767, 40959,
            49151, 57343, 65535
        };

        constexpr AZStd::array<AZ::u32, outputSize> goldenValues3
        {
            0, 536870911, 1073741823,
            1610612735, 2147483647, 2684354559,
            3221225471, 3758096383, 4294967295
        };

        constexpr AZStd::array<float, outputSize> goldenValues4
        {
            0.0f,
            0.125f,
            0.25f,
            0.375f,
            0.5f,
            0.625f,
            0.75f,
            0.875f,
            1.0f
        };

        auto testCommon = [imageDimensions](auto type, auto outFormat, auto pFormat, const auto& goldenValues)
        {
            using baseType = decltype(type);

            ImageSettings settings;
            settings.m_rgbTransform = ChannelExportTransform::MAX;
            settings.m_alphaTransform = AlphaExportTransform::MULTIPLY;
            settings.m_format = outFormat;
            settings.m_useR = true;
            settings.m_useG = true;
            settings.m_useB = true;
            settings.m_useA = true;
            settings.m_autoScale = true;
            settings.m_scaleRangeMin = 0.0f;
            settings.m_scaleRangeMax = 255.0f;

            constexpr auto numChannels = 1;
            constexpr auto bytesPerPixel = numChannels * sizeof(baseType);

            auto inputData = Detail::GenerateInput<baseType, decltype(imageDimensions)::value, numChannels>();

            auto asset = Detail::SetupAssetAndConvert(inputData, imageDimensions,
                pFormat, bytesPerPixel, settings);

            Detail::VerifyResult(*asset, goldenValues, [](auto a, auto b)
            {
                if constexpr (AZStd::is_floating_point_v<decltype(a)>)
                {
                    EXPECT_NEAR(a, b, 0.1);
                }
                else
                {
                    EXPECT_EQ(a, b);
                }
            });
        };

        testCommon(AZ::u8{}, ExportFormat::U8, ImageProcessing::EPixelFormat::ePixelFormat_R8, goldenValues1);
        testCommon(AZ::u16{}, ExportFormat::U16, ImageProcessing::EPixelFormat::ePixelFormat_R16, goldenValues2);
        testCommon(AZ::u32{}, ExportFormat::U32, ImageProcessing::EPixelFormat::ePixelFormat_R32, goldenValues3);
        testCommon(float{}, ExportFormat::F32, ImageProcessing::EPixelFormat::ePixelFormat_R32F, goldenValues4);
    }

    TEST_F(ImageAssetTest, GradientImageAssetTransformsSuccessful)
    {
        // Verify different transforms.

        using namespace GradientSignal;

        constexpr auto imageDimensions = 10;
        constexpr auto numChannels = 4;
        constexpr auto bytesPerPixel = 8;
        constexpr auto outputSize = imageDimensions* imageDimensions;

        ImageSettings settings;
        settings.m_rgbTransform = ChannelExportTransform::AVERAGE;
        settings.m_alphaTransform = AlphaExportTransform::ADD;
        settings.m_format = ExportFormat::F32;
        settings.m_useR = true;
        settings.m_useG = true;
        settings.m_useB = true;
        settings.m_useA = true;
        settings.m_autoScale = true;

        auto inputData = Detail::GenerateInput<AZ::u16, imageDimensions, numChannels>();

        auto asset = Detail::SetupAssetAndConvert(inputData, imageDimensions,
            ImageProcessing::EPixelFormat::ePixelFormat_R16G16B16A16, bytesPerPixel,
            settings);

        // 0, 1, 2, ... -> (RGB / 3 + A) = 2N + 4
        // Min = 0, Max = 796
        AZStd::array<float, outputSize> expectedValues1;

        for (AZ::u32 i = 0, index = 0; i < inputData.size(); i += numChannels)
        {
            expectedValues1[index++] = (2 * i + 4) / 796.0f;
        }

        Detail::VerifyResult(*asset, expectedValues1, [](auto a, auto b)
        {
            EXPECT_NEAR(a, b, 0.01);
        });

        // 0, 1, 3, ... (R + G) / 2 - A
        settings.m_alphaTransform = AlphaExportTransform::SUBTRACT;
        settings.m_useB = false;

        asset = Detail::SetupAssetAndConvert(inputData, imageDimensions,
            ImageProcessing::EPixelFormat::ePixelFormat_R16G16B16A16, bytesPerPixel,
            settings);

        // Assertion: (N + N + 1) / 2 - (N + 3) = -5 / 2
        // All values equal -5 / 2; range is locked to 1.0f.
        constexpr AZStd::array<float, outputSize> goldenValues2
        {
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f
        };

        Detail::VerifyResult(*asset, goldenValues2, [](auto a, auto b)
        {
            EXPECT_NEAR(a, b, 0.01);
        });
    }

    TEST_F(ImageAssetTest, GradientImageAssetTerrariumSuccessful)
    {
        // Verify Terrarium format works as expected.

        using namespace GradientSignal;

        constexpr auto imageDimensions = 10;
        constexpr auto numChannels = 4;
        constexpr auto bytesPerPixel = 16;
        constexpr auto outputSize = imageDimensions * imageDimensions;

        ImageSettings settings;
        settings.m_rgbTransform = ChannelExportTransform::TERRARIUM;
        settings.m_alphaTransform = AlphaExportTransform::ADD;
        settings.m_format = ExportFormat::F32;
        settings.m_useR = true;
        settings.m_useG = true;
        settings.m_useB = true;
        settings.m_useA = true;
        settings.m_autoScale = true;

        auto inputData = Detail::GenerateInput<float, imageDimensions, numChannels>();

        auto asset = Detail::SetupAssetAndConvert(inputData, imageDimensions,
            ImageProcessing::EPixelFormat::ePixelFormat_R32G32B32A32F, bytesPerPixel,
            settings);

        // 0 - 400,  (red * 256 + green + blue / 256) - 32768
        AZStd::array<float, outputSize> expectedValues;

        for (AZ::u32 i = 0, index = 0; i < inputData.size(); i += numChannels)
        {
            constexpr auto terrarium = [](auto r)
            {
                auto g = r + 1;
                auto b = r + 2;

                return (r * 256.0f + g + b / 256.0f) - 32768.0f;
            };

            constexpr auto min = terrarium(0) + 3;
            constexpr auto max = terrarium(396) + 399;

            expectedValues[index++] = (terrarium(i) + i + 3 - min) / (max - min);
        }

        Detail::VerifyResult(*asset, expectedValues, [](auto a, auto b)
        {
            EXPECT_NEAR(a, b, 0.05);
        });
    }
}
