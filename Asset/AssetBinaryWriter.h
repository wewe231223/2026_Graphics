// Asset Binary Format Specification:
//┌─────────────────────────────────────────────────────────────────────────┐
//│                            [HEADER SECTION]                             │
//├──────────────┬───────────────┬──────────────────────────────────────────┤
//│    OFFSET    │      NAME     │                DATA TYPE                 │
//├──────────────┼───────────────┼──────────────────────────────────────────┤
//│    0x00      │     Magic     │  char[4]("FBXB")                         │
//│    0x04      │ FormatVersion │  uint32                                  │
//└──────────────┴───────────────┴──────────────────────────────────────────┘
//
//                               │
//                               ▼
//
//┌─────────────────────────────────────────────────────────────────────────┐
//│                              [BODY SECTION]                             │
//├──────────────┬───────────────┬──────────────────────────────────────────┤
//│    0x08      │   NodeCount   │  uint64(Number of Node Records)          │
//└──────────────┴───────────────┴──────────────────────────────────────────┘
//
//       ┌────────────────────────────────────────────────────────┐
//       │[REPEATING NODE RECORD]                                 │
//       │(Repeats 'NodeCount' times)                             │
//       ├───────────────────┬────────────────────────────────────┤
//       │ Name              │ string                             │
//       │ ParentNodeIndex   │ int32(-1 for Root)                 │
//       │ NodeToParent      │ Mat4(4x4 Matrix)                   │
//       │ GeometryToNode    │ Mat4(4x4 Matrix)                   │
//       ├───────────────────┴────────────────────────────────────┤
//       │             < VertexAttributes >                       │
//       ├───────────────────┬────────────────────────────────────┤
//       │ Positions         │ Vec3[]                             │
//       │ Normals           │ Vec3[]                             │
//       │ TexCoords         │ Vec2[][MAX_TEXCOORDS]              │
//       │ Colors            │ Vec4[]                             │
//       │ Tangents          │ Vec3[]                             │
//       │ Bitangents        │ Vec3[]                             │
//       │ BoneIndices       │ UVec4[]                            │
//       │ BoneWeights       │ Vec4[]                             │
//       ├───────────────────┴────────────────────────────────────┤
//       │ Indices           │ uint32[]                           │
//       ├───────────────────┴────────────────────────────────────┤
//       │                 < SubMesh[] >                          │
//       ├───────────────────┬────────────────────────────────────┤
//       │ IndexOffset       │ uint64                             │
//       │ IndexCount        │ uint64                             │
//       │ MatGroupItemIndex │ uint64                             │
//       └───────────────────┴────────────────────────────────────┘

#pragma once
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include "AssetBundle.h"

namespace asset {
    class AssetBinaryWriter final {
    public:
        AssetBinaryWriter();
        ~AssetBinaryWriter() = default;

        AssetBinaryWriter(const AssetBinaryWriter& Other) = delete;
        AssetBinaryWriter& operator=(const AssetBinaryWriter& Other) = delete;
        AssetBinaryWriter(AssetBinaryWriter&& Other) noexcept = delete;
        AssetBinaryWriter& operator=(AssetBinaryWriter&& Other) noexcept = delete;

    public:
        bool WriteToFile(const std::string& Path, const AssetBundle& Bundle);

    private:
        void WriteHeader();
        void WriteModelResult(const ModelResult& Result);
        void WriteNodes(const std::vector<const ModelNode*>& Nodes, const std::unordered_map<const ModelNode*, std::uint32_t>& NodeIndices);
        void WriteNode(const ModelNode& Node, const std::unordered_map<const ModelNode*, std::uint32_t>& NodeIndices);
        void WriteVertexAttributes(const VertexAttributes& Attributes);
        void WriteSubMeshes(const std::vector<ModelNode::SubMesh>& SubMeshes);
        void WriteVec2Array(std::span<const Vec2> Values);
        void WriteVec3Array(std::span<const Vec3> Values);
        void WriteVec4Array(std::span<const Vec4> Values);
        void WriteUvec4Array(std::span<const UVec4> Values);
        void WriteUint32Array(std::span<const std::uint32_t> Values);
        void WriteUint64Array(std::span<const std::uint64_t> Values);

        void WriteString(const std::string& Value);
        void WriteUint8(std::uint8_t Value);
        void WriteUint16(std::uint16_t Value);
        void WriteUint32(std::uint32_t Value);
        void WriteUint64(std::uint64_t Value);
        void WriteInt32(std::int32_t Value);
        void WriteInt64(std::int64_t Value);
        void WriteFloat(float Value);
        void WriteBool(bool Value);
        void WriteVec2(const Vec2& Value);
        void WriteVec3(const Vec3& Value);
        void WriteVec4(const Vec4& Value);
        void WriteMat4(const Mat4& Value);
        void WriteBytes(const void* Data, std::size_t Size);

    private:
        std::ofstream mStream{};
    };
}
