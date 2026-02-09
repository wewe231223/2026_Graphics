/*
 * ============================================================================
 * FBXB BINARY FORMAT (v2) SPECIFICATION
 * ============================================================================
 *
 * [ HEADER ]
 * +----------+----------+---------------------------------------------------+
 * | Magic    | char[4]  | "FBXB"                                            |
 * | Version  | uint32   | 2                                                 |
 * +----------+----------+---------------------------------------------------+
 *
 * [ MATERIALS ]
 * +---------------+--------+------------------------------------------------+
 * | MaterialCount | uint64 | Total number of materials                      |
 * +---------------+--------+------------------------------------------------+
 * | [ Material Block ] x MaterialCount                                      |
 * |  +---------------+--------+---------------------------------------------+
 * |  | PbrFlag       | uint8  | Physically Based Rendering flag             |
 * |  | PropertyCount | uint64 | Number of properties in this material       |
 * |  +---------------+--------+---------------------------------------------+
 * |  | [ Property Block ] x PropertyCount                                   |
 * |  |  +----------+--------+-----------------------------------------------+
 * |  |  | Type     | uint16 | Property type identifier                     |
 * |  |  | MapKind  | uint8  | 0:None, 1:Real(f32), 2:Int(i64), 3:Bool(u8),  |
 * |  |  |          |        | 4:Vec2, 5:Vec3, 6:Vec4, 7:String(u64+char[])  |
 * |  |  | Payload  | mixed  | (Data size varies based on MapKind)          |
 * |  |  +----------+--------+-----------------------------------------------+
 *
 * [ NODES ] (DFS Order)
 * +---------------+--------+------------------------------------------------+
 * | NodeCount     | uint64 | Total number of nodes                          |
 * +---------------+--------+------------------------------------------------+
 * | [ Node Block ] x NodeCount                                              |
 * |  +------------------+----------+----------------------------------------+
 * |  | Name             | String   | (uint64 Length + char[Length])         |
 * |  | ParentIndex      | int32    | -1 if root                             |
 * |  | NodeToParent     | mat4     | 4x4 Transformation matrix              |
 * |  | GeometryToNode   | mat4     | 4x4 Offset matrix                      |
 * |  +------------------+----------+----------------------------------------+
 * |  | VertexAttributes | (Nested) | For each attribute: uint64 Count + Raw |
 * |  |                  |          | [Pos, Norm, UV[4], Col, Tan, Bitan,    |
 * |  |                  |          |  BoneIdx, BoneWeight]                  |
 * |  +------------------+----------+----------------------------------------+
 * |  | Indices          | (Nested) | uint64 Count + uint32[Count]           |
 * |  | SubMeshes        | (Nested) | uint64 Count + SubMesh[Count]          |
 * |  +------------------+----------+----------------------------------------+
 *
 * [ SubMesh ]
 * +----------------+--------+-----------------------------------------------+
 * | IndexOffset    | uint64 | Start index in the node index buffer          |
 * | IndexCount     | uint64 | Number of indices to draw                     |
 * | MaterialIndex  | uint64 | Material reference index                      |
 * +----------------+--------+-----------------------------------------------+
 *
 * [ COMPATIBILITY ]
 * Version 1 stores a MaterialIndices array instead of SubMeshes. When reading
 * v1, the first material index is used to create a single SubMesh that spans
 * the full index buffer.
 */
#pragma once

#include <fstream>
#include <string>
#include <vector>

#include "AssetBundle.h"

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
        bool ReadFromFile(const std::string& Path, AssetBundle& Bundle);

    private:
        bool ReadHeader();
        void ReadMaterials(std::vector<Material>& Materials);
        Material ReadMaterial();
        MaterialProperty ReadMaterialProperty();
        MaterialMap ReadMaterialMap(MaterialMapKind Kind);
        void ReadModelResult(ModelResult& Result);
        void ReadNodes(ModelResult& Result, std::uint64_t NodeCount, std::vector<ModelNode*>& Nodes);
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
