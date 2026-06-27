#include "Core/DX/FsrParameter.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "Utility/ErrorHandler.h"

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

namespace {
    bool TryReadFileText(const std::filesystem::path& Path, std::string& OutText) {
        std::ifstream Input{ Path, std::ios::binary };
        if (Input.is_open() == false) {
            return false;
        }

        OutText = std::string{ std::istreambuf_iterator<char>{ Input }, std::istreambuf_iterator<char>{} };
        if (OutText.size() >= 3 && static_cast<unsigned char>(OutText[0]) == 0xEF && static_cast<unsigned char>(OutText[1]) == 0xBB && static_cast<unsigned char>(OutText[2]) == 0xBF) {
            OutText.erase(0, 3);
        }

        return true;
    }

    bool ReadBool(const rapidjson::Value& ObjectValue, const char* Name, bool DefaultValue) {
        if (ObjectValue.HasMember(Name) == false || ObjectValue[Name].IsBool() == false) {
            return DefaultValue;
        }

        return ObjectValue[Name].GetBool();
    }

    float ReadFloat(const rapidjson::Value& ObjectValue, const char* Name, float DefaultValue) {
        if (ObjectValue.HasMember(Name) == false || ObjectValue[Name].IsNumber() == false) {
            return DefaultValue;
        }

        return ObjectValue[Name].GetFloat();
    }

    bool TryParseQualityMode(std::string_view Text, Core::DX::FsrQualityMode& OutQualityMode) {
        if (Text == "NativeAA") {
            OutQualityMode = Core::DX::FsrQualityMode::NativeAA;
            return true;
        }

        if (Text == "Quality") {
            OutQualityMode = Core::DX::FsrQualityMode::Quality;
            return true;
        }

        if (Text == "Balanced") {
            OutQualityMode = Core::DX::FsrQualityMode::Balanced;
            return true;
        }

        if (Text == "Performance") {
            OutQualityMode = Core::DX::FsrQualityMode::Performance;
            return true;
        }

        if (Text == "UltraPerformance") {
            OutQualityMode = Core::DX::FsrQualityMode::UltraPerformance;
            return true;
        }

        if (Text == "Custom") {
            OutQualityMode = Core::DX::FsrQualityMode::Custom;
            return true;
        }

        return false;
    }

    Core::DX::FsrQualityMode ReadQualityMode(const rapidjson::Value& ObjectValue, const char* Name, Core::DX::FsrQualityMode DefaultValue) {
        if (ObjectValue.HasMember(Name) == false || ObjectValue[Name].IsString() == false) {
            return DefaultValue;
        }

        Core::DX::FsrQualityMode QualityMode{ DefaultValue };
        if (TryParseQualityMode(std::string_view{ ObjectValue[Name].GetString(), ObjectValue[Name].GetStringLength() }, QualityMode) == false) {
            ErrorHandler::report("FsrParameterFile", std::string{ "Invalid FSR quality mode: " } + ObjectValue[Name].GetString(), ErrorHandler::Level::Warning);
        }

        return QualityMode;
    }

    float ComputeHalton(std::uint32_t Index, std::uint32_t Base) {
        float Result{};
        float Fraction{ 1.0f / static_cast<float>(Base) };
        std::uint32_t Value{ Index };
        while (Value > 0u) {
            Result += Fraction * static_cast<float>(Value % Base);
            Value /= Base;
            Fraction /= static_cast<float>(Base);
        }

        return Result;
    }
}

namespace Core {
    namespace DX {
        bool FsrParameterFile::Read(const std::filesystem::path& Path, FsrParameter& OutParameter) const {
            FsrParameter Parameter{};
            std::string JsonText{};
            if (TryReadFileText(Path, JsonText) == false) {
                OutParameter = Parameter;
                ErrorHandler::report("FsrParameterFile", "FSR parameter file could not be opened: " + Path.string(), ErrorHandler::Level::Warning);
                return false;
            }

            rapidjson::Document Document{};
            Document.Parse<rapidjson::kParseCommentsFlag | rapidjson::kParseTrailingCommasFlag>(JsonText.c_str());
            if (Document.HasParseError() == true || Document.IsObject() == false) {
                const std::string ParseError{ rapidjson::GetParseError_En(Document.GetParseError()) };
                OutParameter = Parameter;
                ErrorHandler::report("FsrParameterFile", "FSR parameter parse failed: " + ParseError, ErrorHandler::Level::Warning);
                return false;
            }

            Parameter.mEnabled = ReadBool(Document, "Enabled", Parameter.mEnabled);
            Parameter.mJitterEnabled = ReadBool(Document, "JitterEnabled", Parameter.mJitterEnabled);
            Parameter.mSharpeningEnabled = ReadBool(Document, "SharpeningEnabled", Parameter.mSharpeningEnabled);
            Parameter.mHighDynamicRange = ReadBool(Document, "HighDynamicRange", Parameter.mHighDynamicRange);
            Parameter.mAutoExposureEnabled = ReadBool(Document, "AutoExposureEnabled", Parameter.mAutoExposureEnabled);
            Parameter.mDepthInverted = ReadBool(Document, "DepthInverted", Parameter.mDepthInverted);
            Parameter.mDepthInfinite = ReadBool(Document, "DepthInfinite", Parameter.mDepthInfinite);
            Parameter.mDebugChecking = ReadBool(Document, "DebugChecking", Parameter.mDebugChecking);
            Parameter.mDebugVisualization = ReadBool(Document, "DebugVisualization", Parameter.mDebugVisualization);
            Parameter.mMotionVectorsJitterCancellation = ReadBool(Document, "MotionVectorsJitterCancellation", Parameter.mMotionVectorsJitterCancellation);
            Parameter.mResetHistory = ReadBool(Document, "ResetHistory", Parameter.mResetHistory);
            Parameter.mQualityMode = ReadQualityMode(Document, "QualityMode", Parameter.mQualityMode);
            Parameter.mSharpness = std::clamp(ReadFloat(Document, "Sharpness", Parameter.mSharpness), 0.0f, 1.0f);
            Parameter.mCustomUpscaleRatio = std::max(ReadFloat(Document, "CustomUpscaleRatio", Parameter.mCustomUpscaleRatio), 1.0f);
            Parameter.mRenderScaleOverride = std::clamp(ReadFloat(Document, "RenderScaleOverride", Parameter.mRenderScaleOverride), 0.0f, 1.0f);

            OutParameter = Parameter;
            return true;
        }

        float ResolveFsrUpscaleRatio(FsrQualityMode QualityMode, float CustomUpscaleRatio) {
            switch (QualityMode) {
                case FsrQualityMode::NativeAA:
                    return 1.0f;
                case FsrQualityMode::Quality:
                    return 1.5f;
                case FsrQualityMode::Balanced:
                    return 1.7f;
                case FsrQualityMode::Performance:
                    return 2.0f;
                case FsrQualityMode::UltraPerformance:
                    return 3.0f;
                case FsrQualityMode::Custom:
                    return std::max(CustomUpscaleRatio, 1.0f);
                default:
                    return 1.5f;
            }
        }

        std::uint32_t ResolveFsrRenderSize(std::uint32_t DisplaySize, float UpscaleRatio) {
            const float EffectiveUpscaleRatio{ std::max(UpscaleRatio, 1.0f) };
            return std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(std::ceil(static_cast<float>(DisplaySize) / EffectiveUpscaleRatio)));
        }

        std::int32_t ResolveFsrJitterPhaseCount(std::uint32_t RenderWidth, std::uint32_t DisplayWidth) {
            if (RenderWidth == 0u || DisplayWidth == 0u) {
                return 1;
            }

            const float Scale{ static_cast<float>(DisplayWidth) / static_cast<float>(RenderWidth) };
            return std::max(1, static_cast<std::int32_t>(std::ceil(8.0f * Scale * Scale)));
        }

        FsrJitterSample BuildFsrJitterSample(std::uint32_t JitterIndex, std::uint32_t RenderWidth, std::uint32_t RenderHeight, std::uint32_t DisplayWidth) {
            FsrJitterSample Sample{};
            Sample.mPhaseCount = ResolveFsrJitterPhaseCount(RenderWidth, DisplayWidth);
            Sample.mIndex = static_cast<std::int32_t>(JitterIndex % static_cast<std::uint32_t>(Sample.mPhaseCount));
            const std::uint32_t HaltonIndex{ static_cast<std::uint32_t>(Sample.mIndex) + 1u };
            Sample.mPixelOffset = DirectX::SimpleMath::Vector2{ ComputeHalton(HaltonIndex, 2u) - 0.5f, ComputeHalton(HaltonIndex, 3u) - 0.5f };
            const float RenderWidthFloat{ static_cast<float>(std::max<std::uint32_t>(1u, RenderWidth)) };
            const float RenderHeightFloat{ static_cast<float>(std::max<std::uint32_t>(1u, RenderHeight)) };
            Sample.mNdcOffset = DirectX::SimpleMath::Vector2{ (2.0f * Sample.mPixelOffset.x) / RenderWidthFloat, (-2.0f * Sample.mPixelOffset.y) / RenderHeightFloat };
            return Sample;
        }
    }
}
