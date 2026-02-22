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
