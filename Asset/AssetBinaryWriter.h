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
 */
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
        void WriteMaterials(const std::vector<Material>& Materials);
        void WriteMaterial(const Material& MaterialData);
        void WriteMaterialProperty(const MaterialProperty& Property);
        void WriteMaterialMap(const MaterialMap& Map);
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
