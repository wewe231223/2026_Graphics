#pragma once

#include <cstdint>

#if __has_include("SimpleMath.h")
    #define _DIRECTX_MATH_ENABLE
    #include "SimpleMath.h"
    #pragma comment(lib, "DirectXTK12.lib")
#endif 

namespace DirectX::SimpleMath {
    struct Vector2;
    struct Vector3;
    struct Vector4;
    struct Matrix;
}

namespace asset {
    struct Vec2 final {
    public:
        Vec2();
        Vec2(float X, float Y);

        float mX;
        float mY;
    };

    struct Vec3 final {
    public:
        Vec3();
        Vec3(float X, float Y, float Z);

        float mX;
        float mY;
        float mZ;
    };

    struct Vec4 final {
    public:
        Vec4();
        Vec4(float X, float Y, float Z, float W);

        float mX;
        float mY;
        float mZ;
        float mW;
    };

    struct UVec4 final {
    public:
        UVec4();
        UVec4(std::uint32_t X, std::uint32_t Y, std::uint32_t Z, std::uint32_t W);

        std::uint32_t mX;
        std::uint32_t mY;
        std::uint32_t mZ;
        std::uint32_t mW;
    };

    struct Mat4 final {
    public:
        Mat4();
        explicit Mat4(float Diagonal);

        float* Data();
        const float* Data() const;

        float mValue[4][4];
    };

#ifdef _DIRECTX_MATH_ENABLE
    DirectX::SimpleMath::Vector2 ToSimpleMath(const Vec2& Value);
    DirectX::SimpleMath::Vector3 ToSimpleMath(const Vec3& Value);
    DirectX::SimpleMath::Vector4 ToSimpleMath(const Vec4& Value);
    DirectX::SimpleMath::Vector4 ToSimpleMath(const UVec4& Value);
    DirectX::SimpleMath::Matrix ToSimpleMath(const Mat4& Value);
#endif 

}
