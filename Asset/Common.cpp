#include "Common.h"

namespace asset {
    MaterialMap::MaterialMap() {
    }

    MaterialMap::MaterialMap(float Value)
        : mKind{ MaterialMapKind::Real } {
        mValue.mReal = Value;
    }

    MaterialMap::MaterialMap(std::int64_t Value)
        : mKind{ MaterialMapKind::Int } {
        mValue.mInt = Value;
    }

    MaterialMap::MaterialMap(bool Value)
        : mKind{ MaterialMapKind::Bool } {
        mValue.mBool = Value;
    }

    MaterialMap::MaterialMap(const Vec2& Value)
        : mKind{ MaterialMapKind::Vec2 } {
        mValue.mVec2[0] = Value.x;
        mValue.mVec2[1] = Value.y;
    }

    MaterialMap::MaterialMap(const Vec3& Value)
        : mKind{ MaterialMapKind::Vec3 } {
        mValue.mVec3[0] = Value.x;
        mValue.mVec3[1] = Value.y;
        mValue.mVec3[2] = Value.z;
    }

    MaterialMap::MaterialMap(const Vec4& Value)
        : mKind{ MaterialMapKind::Vec4 } {
        mValue.mVec4[0] = Value.x;
        mValue.mVec4[1] = Value.y;
        mValue.mVec4[2] = Value.z;
        mValue.mVec4[3] = Value.w;
    }

    MaterialMap::MaterialMap(std::string Value)
        : mKind{ MaterialMapKind::String }
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
}
