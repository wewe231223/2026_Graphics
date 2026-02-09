#include "Common.h"

using namespace asset;

MaterialMap::MaterialMap()
    : mKind{ MaterialMapKind::None }
    , mValue{}
    , mString{} {
}

MaterialMap::MaterialMap(float Value)
    : mKind{ MaterialMapKind::Real }
    , mValue{}
    , mString{} {
    mValue.mReal = Value;
}

MaterialMap::MaterialMap(std::int64_t Value)
    : mKind{ MaterialMapKind::Int }
    , mValue{}
    , mString{} {
    mValue.mInt = Value;
}

MaterialMap::MaterialMap(bool Value)
    : mKind{ MaterialMapKind::Bool }
    , mValue{}
    , mString{} {
    mValue.mBool = Value;
}

MaterialMap::MaterialMap(const Vec2& Value)
    : mKind{ MaterialMapKind::Vec2 }
    , mValue{}
    , mString{} {
    mValue.mVec2[0] = Value.mX;
    mValue.mVec2[1] = Value.mY;
}

MaterialMap::MaterialMap(const Vec3& Value)
    : mKind{ MaterialMapKind::Vec3 }
    , mValue{}
    , mString{} {
    mValue.mVec3[0] = Value.mX;
    mValue.mVec3[1] = Value.mY;
    mValue.mVec3[2] = Value.mZ;
}

MaterialMap::MaterialMap(const Vec4& Value)
    : mKind{ MaterialMapKind::Vec4 }
    , mValue{}
    , mString{} {
    mValue.mVec4[0] = Value.mX;
    mValue.mVec4[1] = Value.mY;
    mValue.mVec4[2] = Value.mZ;
    mValue.mVec4[3] = Value.mW;
}

MaterialMap::MaterialMap(std::string Value)
    : mKind{ MaterialMapKind::String }
    , mValue{}
    , mString{ std::move(Value) } {
}

MaterialMapKind MaterialMap::GetKind() const {
    return mKind;
}

float MaterialMap::GetReal() const {
    return mValue.mReal;
}

std::int64_t MaterialMap::GetInt() const {
    return mValue.mInt;
}

bool MaterialMap::GetBool() const {
    return mValue.mBool;
}

Vec2 MaterialMap::GetVec2() const {
    return Vec2{ mValue.mVec2[0], mValue.mVec2[1] };
}

Vec3 MaterialMap::GetVec3() const {
    return Vec3{ mValue.mVec3[0], mValue.mVec3[1], mValue.mVec3[2] };
}

Vec4 MaterialMap::GetVec4() const {
    return Vec4{ mValue.mVec4[0], mValue.mVec4[1], mValue.mVec4[2], mValue.mVec4[3] };
}

const std::string& MaterialMap::GetString() const {
    return mString;
}

std::size_t VertexAttributes::VertexCount() const {
    return Positions.size();
}

bool VertexAttributes::Empty() const {
    return Positions.empty();
}

void VertexAttributes::Reserve(std::size_t Count) {
    Positions.reserve(Count);
    Normals.reserve(Count);
    Colors.reserve(Count);
    Tangents.reserve(Count);
    Bitangents.reserve(Count);
    BoneIndices.reserve(Count);
    BoneWeights.reserve(Count);
    for (std::vector<Vec2>& Set : TexCoords) {
        Set.reserve(Count);
    }
}

void VertexAttributes::Resize(std::size_t Count) {
    Positions.resize(Count);
    Normals.resize(Count);
    Colors.resize(Count);
    Tangents.resize(Count);
    Bitangents.resize(Count);
    BoneIndices.resize(Count);
    BoneWeights.resize(Count);
    for (std::vector<Vec2>& Set : TexCoords) {
        Set.resize(Count);
    }
}
