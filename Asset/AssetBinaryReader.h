// Asset Binary Format Specification (Current: Version 9):
//┌──────────────────────────────────────────────────────────────────────────────┐
//│                                [HEADER SECTION]                              │
//├──────────────┬────────────────┬──────────────────────────────────────────────┤
//│    OFFSET    │      NAME      │                  DATA TYPE                   │
//├──────────────┼────────────────┼──────────────────────────────────────────────┤
//│    0x00      │     Magic      │ char[4]("FBXB")                              │
//│    0x04      │ FormatVersion  │ uint32 (9)                                   │
//└──────────────┴────────────────┴──────────────────────────────────────────────┘
//
//                                   │
//                                   ▼
//
//┌──────────────────────────────────────────────────────────────────────────────┐
//│                                 [BODY SECTION]                               │
//├──────────────┬────────────────┬──────────────────────────────────────────────┤
//│    0x08      │   NodeCount    │ uint64 (Number of Node Records)              │
//│    0x10      │ UnifiedSkinBoneRootNodeName │ string                         │
//└──────────────┴────────────────┴──────────────────────────────────────────────┘
//
//       ┌──────────────────────────────────────────────────────────────────┐
//       │ [REPEATING NODE RECORD]                                          │
//       │ (Repeats 'NodeCount' times)                                      │
//       ├───────────────────────┬──────────────────────────────────────────┤
//       │ Name                  │ string (uint64 length + bytes)           │
//       │ ParentNodeIndex       │ int32 (-1 for Root)                      │
//       │ NodeToParent          │ Mat4                                     │
//       ├───────────────────────┴──────────────────────────────────────────┤
//       │ < BoneInfos >                                                    │
//       ├───────────────────────┬──────────────────────────────────────────┤
//       │ BoneInfoCount         │ uint64                                   │
//       │ SkinArrayIndex        │ uint32                                   │
//       │ JointArrayIndex       │ uint32                                   │
//       │ BoneName              │ string                                   │
//       │ InverseBindMatrix     │ Mat4                                     │
//       ├───────────────────────┴──────────────────────────────────────────┤
//       │ IsSkinnedMesh         │ bool (stored as uint8)                   │
//       ├───────────────────────┴──────────────────────────────────────────┤
//       │ < VertexAttributes >                                             │
//       ├───────────────────────┬──────────────────────────────────────────┤
//       │ Positions             │ Vec3[] (uint64 count + raw bytes)        │
//       │ Normals               │ Vec3[]                                   │
//       │ TexCoords[0..N-1]     │ Vec2[] for each MAX_TEXCOORDS slot       │
//       │ Colors                │ Vec4[]                                   │
//       │ Tangents              │ Vec3[]                                   │
//       │ Bitangents            │ Vec3[]                                   │
//       │ BoneIndices           │ UVec4[]                                  │
//       │ BoneWeights           │ Vec4[]                                   │
//       ├───────────────────────┴──────────────────────────────────────────┤
//       │ Indices               │ uint32[]                                 │
//       ├───────────────────────┴──────────────────────────────────────────┤
//       │ < SubMeshes >                                                    │
//       ├───────────────────────┬──────────────────────────────────────────┤
//       │ SubMeshCount          │ uint64                                   │
//       │ IndexOffset           │ uint64                                   │
//       │ IndexCount            │ uint64                                   │
//       │ MaterialGroupItemIndex│ uint64                                   │
//       └───────────────────────┴──────────────────────────────────────────┘
#pragma once

#include <fstream>
#include <span>
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
        void ReadNodes(ModelResult& Result, std::uint64_t NodeCount, const std::string& UnifiedSkinBoneRootNodeName, std::vector<ModelNode*>& Nodes);
        std::vector<ModelBoneInfo> ReadBoneInfos();
        void ReadSkinnedMeshFlag(ModelNode& Node);
        void ReadSkinBoneRootNodeName(ModelNode& Node);
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
        Mat4 ReadMat4();
        void ReadBytes(void* Data, std::size_t Size);

    private:
        std::ifstream mStream{};
        std::uint32_t mFormatVersion{ 0 };
    };
}
