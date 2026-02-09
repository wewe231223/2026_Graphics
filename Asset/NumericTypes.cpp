#include "NumericTypes.h"

namespace asset {
    using namespace DirectX::SimpleMath;

    Vec2::Vec2()
        : mX{ 0.0f },
		mY{ 0.0f } {
    }

    Vec2::Vec2(float X, float Y)
        : mX{ X },
		mY{ Y } {
    }

    Vec3::Vec3()
        : mX{ 0.0f },
		mY{ 0.0f },
		mZ{ 0.0f } {
    }

    Vec3::Vec3(float X, float Y, float Z)
        : mX{ X },
		mY{ Y },
		mZ{ Z } {
    }

    Vec4::Vec4()
        : mX{ 0.0f },
		mY{ 0.0f },
		mZ{ 0.0f },
		mW{ 0.0f } {
    }

    Vec4::Vec4(float X, float Y, float Z, float W)
        : mX{ X },
		mY{ Y },
		mZ{ Z },
		mW{ W } {
    }

    UVec4::UVec4()
        : mX{ 0 },
		mY{ 0 },
		mZ{ 0 },
		mW{ 0 } {
    }

    UVec4::UVec4(std::uint32_t X, std::uint32_t Y, std::uint32_t Z, std::uint32_t W)
        : mX{ X },
		mY{ Y },
		mZ{ Z },
		mW{ W } {
    }

    Mat4::Mat4()
        : mValue{} {
    }

    Mat4::Mat4(float Diagonal)
        : mValue{} {
        mValue[0][0] = Diagonal;
        mValue[1][1] = Diagonal;
        mValue[2][2] = Diagonal;
        mValue[3][3] = Diagonal;
    }

    float* Mat4::Data() {
        return &mValue[0][0];
    }

    const float* Mat4::Data() const {
        return &mValue[0][0];
    }

#ifdef _DIRECTX_MATH_ENABLE
    Vector2 ToSimpleMath(const Vec2& Value) {
        return Vector2{ Value.mX, Value.mY };
    }

    Vector3 ToSimpleMath(const Vec3& Value) {
        return Vector3{ Value.mX, Value.mY, Value.mZ };
    }

    Vector4 ToSimpleMath(const Vec4& Value) {
        return Vector4{ Value.mX, Value.mY, Value.mZ, Value.mW };
    }

    Vector4 ToSimpleMath(const UVec4& Value) {
        return Vector4{ static_cast<float>(Value.mX), static_cast<float>(Value.mY), static_cast<float>(Value.mZ), static_cast<float>(Value.mW) };
    }

    Matrix ToSimpleMath(const Mat4& Value) {
        Matrix result{};
        result._11 = Value.mValue[0][0];
        result._12 = Value.mValue[0][1];
        result._13 = Value.mValue[0][2];
        result._14 = Value.mValue[0][3];
        result._21 = Value.mValue[1][0];
        result._22 = Value.mValue[1][1];
        result._23 = Value.mValue[1][2];
        result._24 = Value.mValue[1][3];
        result._31 = Value.mValue[2][0];
        result._32 = Value.mValue[2][1];
        result._33 = Value.mValue[2][2];
        result._34 = Value.mValue[2][3];
        result._41 = Value.mValue[3][0];
        result._42 = Value.mValue[3][1];
        result._43 = Value.mValue[3][2];
        result._44 = Value.mValue[3][3];
        return result;
    }
#endif 
}
