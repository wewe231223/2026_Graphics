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
//       │ SourceNodeTypedId │ uint32                             │
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
//
//       ┌────────────────────────────────────────────────────────┐
//       │                     [SKELETON DATA]                    │
//       ├───────────────────┬────────────────────────────────────┤
//       │ BoneCount         │ uint64                             │
//       │ ClusterCount      │ uint64                             │
//       │ SkinCount         │ uint64                             │
//       └───────────────────┴────────────────────────────────────┘
//
//       ┌────────────────────────────────────────────────────────┐
//       │                 < Repeating SkeletonBone >             │
//       ├───────────────────┬────────────────────────────────────┤
//       │ Name              │ string                             │
//       │ NodeName          │ string                             │
//       │ NodeTypedId       │ uint32                             │
//       │ BoneTypedId       │ uint32                             │
//       │ Radius            │ float                              │
//       │ RelativeLength    │ float                              │
//       │ IsRoot            │ bool(uint8)                        │
//       │ NodeToParent      │ Mat4                               │
//       │ NodeToWorld       │ Mat4                               │
//       └───────────────────┴────────────────────────────────────┘
//
//       ┌────────────────────────────────────────────────────────┐
//       │               < Repeating SkeletonCluster >            │
//       ├───────────────────┬────────────────────────────────────┤
//       │ Name              │ string                             │
//       │ ClusterTypedId    │ uint32                             │
//       │ SkinDeformerTypedId│ uint32                            │
//       │ BoneIndex         │ uint32                             │
//       │ GeometryToBone    │ Mat4                               │
//       │ MeshNodeToBone    │ Mat4                               │
//       │ BindToWorld       │ Mat4                               │
//       │ GeometryToWorld   │ Mat4                               │
//       └───────────────────┴────────────────────────────────────┘
//
//       ┌────────────────────────────────────────────────────────┐
//       │                < Repeating SkeletonSkin >              │
//       ├───────────────────┬────────────────────────────────────┤
//       │ Name              │ string                             │
//       │ MeshNodeName      │ string                             │
//       │ SkinDeformerTypedId│ uint32                            │
//       │ MeshNodeTypedId   │ uint32                             │
//       │ SkinningMethod    │ uint32                             │
//       │ ClusterIndices    │ uint32[]                           │
//       └───────────────────┴────────────────────────────────────┘
//
// Version compatibility:
// - v1: Node + MaterialIndex(uint64[]) legacy
// - v2~v4: Node + SubMesh[]
// - v5: v4 + Skeleton Data
// - v6: v5 + SourceNodeTypedId in each Node Record

#pragma once
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include "ModelResult.h"

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
        bool WriteToFile(const std::string& Path, const ModelResult& ModelData);

    private:
        void WriteHeader();
        void WriteModelResult(const ModelResult& Result);
        void WriteNodes(const std::vector<const ModelNode*>& Nodes, const std::unordered_map<const ModelNode*, std::uint32_t>& NodeIndices);
        void WriteNode(const ModelNode& Node, const std::unordered_map<const ModelNode*, std::uint32_t>& NodeIndices);
        void WriteSkeletonData(const SkeletonData& SkeletonDataValue);
        void WriteSkeletonBone(const SkeletonBone& BoneData);
        void WriteSkeletonCluster(const SkeletonCluster& ClusterData);
        void WriteSkeletonSkin(const SkeletonSkin& SkinData);
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
