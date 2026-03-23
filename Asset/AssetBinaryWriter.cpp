#include "AssetBinaryWriter.h"

#include <cstring>

using namespace asset;

namespace {
    constexpr std::uint32_t FormatVersion{ 7 };
    constexpr char FormatMagic[4]{ 'F', 'B', 'X', 'B' };
}

AssetBinaryWriter::AssetBinaryWriter() = default;

bool AssetBinaryWriter::WriteToFile(const std::string& Path, const ModelResult& ModelData) {
    mStream = std::ofstream{ Path, std::ios::binary };
    if (!mStream.is_open()) {
        return false;
    }
    WriteHeader();
    WriteModelResult(ModelData);
    return static_cast<bool>(mStream);
}

void AssetBinaryWriter::WriteHeader() {
    WriteBytes(FormatMagic, sizeof(FormatMagic));
    WriteUint32(FormatVersion);
}

void AssetBinaryWriter::WriteModelResult(const ModelResult& Result) {
    std::vector<const ModelNode*> Nodes{};
    std::vector<const ModelNode*> Stack{};
    const ModelNode* Root{ Result.GetRoot() };
    if (Root != nullptr) {
        Stack.push_back(Root);
        while (!Stack.empty()) {
            const ModelNode* Node{ Stack.back() };
            Stack.pop_back();
            Nodes.push_back(Node);
            const std::vector<ModelNode*>& Children{ Node->GetChildren() };
            for (std::size_t Index{ Children.size() }; Index > 0; --Index) {
                Stack.push_back(Children[Index - 1]);
            }
        }
    }

    std::unordered_map<const ModelNode*, std::uint32_t> NodeIndices{};
    NodeIndices.reserve(Nodes.size());
    for (std::uint32_t Index{ 0 }; Index < Nodes.size(); ++Index) {
        NodeIndices.emplace(Nodes[Index], Index);
    }

    WriteUint64(static_cast<std::uint64_t>(Nodes.size()));
    WriteNodes(Nodes, NodeIndices);
}

void AssetBinaryWriter::WriteNodes(const std::vector<const ModelNode*>& Nodes, const std::unordered_map<const ModelNode*, std::uint32_t>& NodeIndices) {
    for (const ModelNode* Node : Nodes) {
        WriteNode(*Node, NodeIndices);
    }
}

void AssetBinaryWriter::WriteNode(const ModelNode& Node, const std::unordered_map<const ModelNode*, std::uint32_t>& NodeIndices) {
    WriteString(Node.GetName());
    const ModelNode* Parent{ Node.GetParent() };
    if (Parent == nullptr) {
        WriteInt32(-1);
    }
    else {
        const auto Found{ NodeIndices.find(Parent) };
        WriteInt32(Found == NodeIndices.end() ? -1 : static_cast<std::int32_t>(Found->second));
    }
    WriteMat4(Node.GetNodeToParent());
    static_cast<void>(NodeIndices);
    WriteBoneInfos(Node.BoneInfos());
    WriteSkinnedMeshFlag(Node);
    WriteVertexAttributes(Node.Vertices());
    WriteUint32Array(Node.Indices());
    WriteSubMeshes(Node.GetSubMeshes());
}

void AssetBinaryWriter::WriteBoneInfos(const std::vector<ModelBoneInfo>& BoneInfos) {
    WriteUint64(static_cast<std::uint64_t>(BoneInfos.size()));
    for (const ModelBoneInfo& BoneInfo : BoneInfos) {
        WriteUint32(BoneInfo.SkinArrayIndex);
        WriteUint32(BoneInfo.JointArrayIndex);
        WriteString(BoneInfo.BoneName);
        WriteMat4(BoneInfo.InverseBindMatrix);
    }
}

void AssetBinaryWriter::WriteSkinnedMeshFlag(const ModelNode& Node) {
    WriteBool(Node.IsSkinnedMesh());
}

void AssetBinaryWriter::WriteVertexAttributes(const VertexAttributes& Attributes) {
    WriteVec3Array(Attributes.Positions);
    WriteVec3Array(Attributes.Normals);
    for (std::size_t Index{ 0 }; Index < Attributes.TexCoords.size(); ++Index) {
        WriteVec2Array(Attributes.TexCoords[Index]);
    }
    WriteVec4Array(Attributes.Colors);
    WriteVec3Array(Attributes.Tangents);
    WriteVec3Array(Attributes.Bitangents);
    WriteUvec4Array(Attributes.BoneIndices);
    WriteVec4Array(Attributes.BoneWeights);
}

void AssetBinaryWriter::WriteSubMeshes(const std::vector<ModelNode::SubMesh>& SubMeshes) {
    WriteUint64(static_cast<std::uint64_t>(SubMeshes.size()));
    for (const ModelNode::SubMesh& SubMesh : SubMeshes) {
        WriteUint64(static_cast<std::uint64_t>(SubMesh.IndexOffset));
        WriteUint64(static_cast<std::uint64_t>(SubMesh.IndexCount));
        WriteUint64(static_cast<std::uint64_t>(SubMesh.MaterialGroupItemIndex));
    }
}

void AssetBinaryWriter::WriteVec2Array(std::span<const Vec2> Values) {
    WriteUint64(static_cast<std::uint64_t>(Values.size()));
    WriteBytes(Values.data(), Values.size_bytes());
}

void AssetBinaryWriter::WriteVec3Array(std::span<const Vec3> Values) {
    WriteUint64(static_cast<std::uint64_t>(Values.size()));
    WriteBytes(Values.data(), Values.size_bytes());
}

void AssetBinaryWriter::WriteVec4Array(std::span<const Vec4> Values) {
    WriteUint64(static_cast<std::uint64_t>(Values.size()));
    WriteBytes(Values.data(), Values.size_bytes());
}

void AssetBinaryWriter::WriteUvec4Array(std::span<const UVec4> Values) {
    WriteUint64(static_cast<std::uint64_t>(Values.size()));
    WriteBytes(Values.data(), Values.size_bytes());
}

void AssetBinaryWriter::WriteUint32Array(std::span<const std::uint32_t> Values) {
    WriteUint64(static_cast<std::uint64_t>(Values.size()));
    WriteBytes(Values.data(), Values.size_bytes());
}

void AssetBinaryWriter::WriteUint64Array(std::span<const std::uint64_t> Values) {
    WriteUint64(static_cast<std::uint64_t>(Values.size()));
    WriteBytes(Values.data(), Values.size_bytes());
}

void AssetBinaryWriter::WriteString(const std::string& Value) {
    WriteUint64(static_cast<std::uint64_t>(Value.size()));
    WriteBytes(Value.data(), Value.size());
}

void AssetBinaryWriter::WriteUint8(std::uint8_t Value) {
    WriteBytes(&Value, sizeof(Value));
}

void AssetBinaryWriter::WriteUint16(std::uint16_t Value) {
    WriteBytes(&Value, sizeof(Value));
}

void AssetBinaryWriter::WriteUint32(std::uint32_t Value) {
    WriteBytes(&Value, sizeof(Value));
}

void AssetBinaryWriter::WriteUint64(std::uint64_t Value) {
    WriteBytes(&Value, sizeof(Value));
}

void AssetBinaryWriter::WriteInt32(std::int32_t Value) {
    WriteBytes(&Value, sizeof(Value));
}

void AssetBinaryWriter::WriteInt64(std::int64_t Value) {
    WriteBytes(&Value, sizeof(Value));
}

void AssetBinaryWriter::WriteFloat(float Value) {
    WriteBytes(&Value, sizeof(Value));
}

void AssetBinaryWriter::WriteBool(bool Value) {
    const std::uint8_t Stored{ static_cast<std::uint8_t>(Value ? 1 : 0) };
    WriteUint8(Stored);
}

void AssetBinaryWriter::WriteVec2(const Vec2& Value) {
    WriteBytes(&Value, sizeof(Value));
}

void AssetBinaryWriter::WriteVec3(const Vec3& Value) {
    WriteBytes(&Value, sizeof(Value));
}

void AssetBinaryWriter::WriteVec4(const Vec4& Value) {
    WriteBytes(&Value, sizeof(Value));
}

void AssetBinaryWriter::WriteMat4(const Mat4& Value) {
    WriteBytes(&Value, sizeof(Value));
}

void AssetBinaryWriter::WriteBytes(const void* Data, std::size_t Size) {
    mStream.write(reinterpret_cast<const char*>(Data), static_cast<std::streamsize>(Size));
}
