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

#pragma once

#include <fstream>
#include <string>
#include <vector>

#include "ModelResult.h"

namespace asset {
    class AssetBinaryReader final {
    public:
        AssetBinaryReader();
        ~AssetBinaryReader() = default;

        AssetBinaryReader(const AssetBinaryReader& Other) = delete;
        AssetBinaryReader& operator=(const AssetBinaryReader& Other) = delete;
        AssetBinaryReader(AssetBinaryReader&& Other) noexcept = delete;
        AssetBinaryReader& operator=(AssetBinaryReader&& Other) noexcept = delete;

    public:
        bool ReadFromFile(const std::string& Path, ModelResult& ModelData);

    private:
        bool ReadHeader();
        void ReadModelResult(ModelResult& Result);
        void ReadNodes(ModelResult& Result, std::uint64_t NodeCount, std::vector<ModelNode*>& Nodes);
        SkeletonData ReadSkeletonData();
        SkeletonBone ReadSkeletonBone();
        SkeletonCluster ReadSkeletonCluster();
        SkeletonSkin ReadSkeletonSkin();
        void ReadVertexAttributes(VertexAttributes& Attributes);
        std::vector<ModelNode::SubMesh> ReadSubMeshes();
        std::vector<Vec2> ReadVec2Array();
        std::vector<Vec3> ReadVec3Array();
        std::vector<Vec4> ReadVec4Array();
        std::vector<UVec4> ReadUvec4Array();
        std::vector<std::uint32_t> ReadUint32Array();
        std::vector<std::uint64_t> ReadUint64Array();

        std::string ReadString();
        std::uint8_t ReadUint8();
        std::uint16_t ReadUint16();
        std::uint32_t ReadUint32();
        std::uint64_t ReadUint64();
        std::int32_t ReadInt32();
        std::int64_t ReadInt64();
        float ReadFloat();
        bool ReadBool();
        Vec2 ReadVec2();
        Vec3 ReadVec3();
        Vec4 ReadVec4();
        Mat4 ReadMat4();
        void ReadBytes(void* Data, std::size_t Size);

    private:
        std::ifstream mStream{};
        std::uint32_t mFormatVersion{ 0 };
    };
}
