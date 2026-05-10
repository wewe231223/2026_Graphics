#include "HeightMapLoader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace {
    float Clamp01(float Value) {
        return std::clamp(Value, 0.0f, 1.0f);
    }

    std::size_t CalculatePixelCount(std::uint32_t Width, std::uint32_t Height) {
        return static_cast<std::size_t>(Width) * static_cast<std::size_t>(Height);
    }

    float ComputeLuminance01From8Bit(const stbi_uc* PixelPointer, int ChannelCount) {
        const float Inverse255{ 1.0f / 255.0f };

        if (ChannelCount == 1 || ChannelCount == 2) {
            const float Height01{ static_cast<float>(PixelPointer[0]) * Inverse255 };
            return Clamp01(Height01);
        }

        const float R{ static_cast<float>(PixelPointer[0]) * Inverse255 };
        const float G{ static_cast<float>(PixelPointer[1]) * Inverse255 };
        const float B{ static_cast<float>(PixelPointer[2]) * Inverse255 };
        const float Height01{ (0.2126f * R) + (0.7152f * G) + (0.0722f * B) };
        return Clamp01(Height01);
    }

    float ComputeLuminance01From16Bit(const stbi_us* PixelPointer, int ChannelCount) {
        const float Inverse65535{ 1.0f / 65535.0f };

        if (ChannelCount == 1 || ChannelCount == 2) {
            const float Height01{ static_cast<float>(PixelPointer[0]) * Inverse65535 };
            return Clamp01(Height01);
        }

        const float R{ static_cast<float>(PixelPointer[0]) * Inverse65535 };
        const float G{ static_cast<float>(PixelPointer[1]) * Inverse65535 };
        const float B{ static_cast<float>(PixelPointer[2]) * Inverse65535 };
        const float Height01{ (0.2126f * R) + (0.7152f * G) + (0.0722f * B) };
        return Clamp01(Height01);
    }

    Game::HeightFieldData LoadHeightField01From8Bit(const std::string& Path) {
        int WidthInt{ 0 };
        int HeightInt{ 0 };
        int ChannelCount{ 0 };
        stbi_uc* ImageData{ stbi_load(Path.c_str(), &WidthInt, &HeightInt, &ChannelCount, 0) };

        if (ImageData == nullptr) {
            throw std::runtime_error{ "Height map decode failed." };
        }

        Game::HeightFieldData Field{};
        Field.Width = static_cast<std::uint32_t>(WidthInt);
        Field.Height = static_cast<std::uint32_t>(HeightInt);

        if (Field.Width < 2 || Field.Height < 2) {
            stbi_image_free(ImageData);
            throw std::runtime_error{ "Height map size must be at least 2x2." };
        }

        if (ChannelCount <= 0) {
            stbi_image_free(ImageData);
            throw std::runtime_error{ "Invalid channel count in height map." };
        }

        const std::size_t PixelCount{ CalculatePixelCount(Field.Width, Field.Height) };
        Field.HeightValues.resize(PixelCount);

        const std::size_t Stride{ static_cast<std::size_t>(ChannelCount) };
        for (std::size_t PixelIndex{ 0 }; PixelIndex < PixelCount; ++PixelIndex) {
            const stbi_uc* PixelPointer{ ImageData + (PixelIndex * Stride) };
            Field.HeightValues[PixelIndex] = ComputeLuminance01From8Bit(PixelPointer, ChannelCount);
        }

        stbi_image_free(ImageData);

        if (Field.HeightValues.size() != PixelCount) {
            throw std::runtime_error{ "Height field buffer size mismatch." };
        }

        return Field;
    }

    Game::HeightFieldData LoadHeightField01From16Bit(const std::string& Path) {
        int WidthInt{ 0 };
        int HeightInt{ 0 };
        int ChannelCount{ 0 };
        stbi_us* ImageData{ stbi_load_16(Path.c_str(), &WidthInt, &HeightInt, &ChannelCount, 0) };

        if (ImageData == nullptr) {
            throw std::runtime_error{ "Height map decode failed." };
        }

        Game::HeightFieldData Field{};
        Field.Width = static_cast<std::uint32_t>(WidthInt);
        Field.Height = static_cast<std::uint32_t>(HeightInt);

        if (Field.Width < 2 || Field.Height < 2) {
            stbi_image_free(ImageData);
            throw std::runtime_error{ "Height map size must be at least 2x2." };
        }

        if (ChannelCount <= 0) {
            stbi_image_free(ImageData);
            throw std::runtime_error{ "Invalid channel count in height map." };
        }

        const std::size_t PixelCount{ CalculatePixelCount(Field.Width, Field.Height) };
        Field.HeightValues.resize(PixelCount);

        const std::size_t Stride{ static_cast<std::size_t>(ChannelCount) };
        for (std::size_t PixelIndex{ 0 }; PixelIndex < PixelCount; ++PixelIndex) {
            const stbi_us* PixelPointer{ ImageData + (PixelIndex * Stride) };
            Field.HeightValues[PixelIndex] = ComputeLuminance01From16Bit(PixelPointer, ChannelCount);
        }

        stbi_image_free(ImageData);

        if (Field.HeightValues.size() != PixelCount) {
            throw std::runtime_error{ "Height field buffer size mismatch." };
        }

        return Field;
    }
}

namespace Game {
    HeightMapLoader::HeightMapLoader() {
    }

    HeightMapLoader::~HeightMapLoader() {
    }

    HeightMapLoader::HeightMapLoader(const HeightMapLoader& Other) {
        (void)Other;
    }

    HeightMapLoader& HeightMapLoader::operator=(const HeightMapLoader& Other) {
        (void)Other;
        return *this;
    }

    HeightMapLoader::HeightMapLoader(HeightMapLoader&& Other) noexcept {
        (void)Other;
    }

    HeightMapLoader& HeightMapLoader::operator=(HeightMapLoader&& Other) noexcept {
        (void)Other;
        return *this;
    }

    HeightFieldData HeightMapLoader::LoadHeightField(const std::string& Path) const {
        if (Path.empty() == true) {
            throw std::runtime_error{ "Height map path is empty." };
        }

        const int Is16Bit{ stbi_is_16_bit(Path.c_str()) };
        if (Is16Bit != 0) {
            return LoadHeightField01From16Bit(Path);
        }

        return LoadHeightField01From8Bit(Path);
    }
}
