#include "AssetBinaryReader.h"

#include <array>
#include <cstring>

using namespace asset;

namespace {
    constexpr std::uint32_t FormatVersion{ 2 };
    constexpr std::array<char, 4> FormatMagic{ 'F', 'B', 'X', 'B' };
}

AssetBinaryReader::AssetBinaryReader() = default;

bool AssetBinaryReader::ReadFromFile(const std::string& Path, AssetBundle& Bundle) {
    mStream = std::ifstream{ Path, std::ios::binary };
    if (!mStream.is_open()) {
        return false;
    }
    Bundle.Clear();
    if (!ReadHeader()) {
        return false;
    }
    ReadMaterials(Bundle.GetMaterials());
    ReadModelResult(Bundle.GetModelResult());
    return static_cast<bool>(mStream);
}

bool AssetBinaryReader::ReadHeader() {
    std::array<char, 4> Magic{};
    ReadBytes(Magic.data(), Magic.size());
    if (Magic != FormatMagic) {
        return false;
    }
    const std::uint32_t Version{ ReadUint32() };
    if (Version != 1 && Version != FormatVersion) {
        return false;
    }
    mFormatVersion = Version;
    return true;
}

void AssetBinaryReader::ReadMaterials(std::vector<Material>& Materials) {
    const std::uint64_t Count{ ReadUint64() };
    Materials.clear();
    Materials.reserve(static_cast<std::size_t>(Count));
    for (std::uint64_t Index{ 0 }; Index < Count; ++Index) {
        Materials.push_back(ReadMaterial());
    }
}

Material AssetBinaryReader::ReadMaterial() {
    Material MaterialData{};
    MaterialData.PBR = ReadBool();
    const std::uint64_t PropertyCount{ ReadUint64() };
    MaterialData.Properties.reserve(static_cast<std::size_t>(PropertyCount));
    for (std::uint64_t Index{ 0 }; Index < PropertyCount; ++Index) {
        MaterialData.Properties.push_back(ReadMaterialProperty());
    }
    return MaterialData;
}

MaterialProperty AssetBinaryReader::ReadMaterialProperty() {
    MaterialProperty Property{};
    Property.Type = static_cast<MaterialType>(ReadUint16());
    const MaterialMapKind Kind{ static_cast<MaterialMapKind>(ReadUint8()) };
    Property.Data = ReadMaterialMap(Kind);
    return Property;
}

MaterialMap AssetBinaryReader::ReadMaterialMap(MaterialMapKind Kind) {
    if (Kind == MaterialMapKind::None) {
        return MaterialMap{};
    }
    if (Kind == MaterialMapKind::Real) {
        return MaterialMap{ ReadFloat() };
    }
    if (Kind == MaterialMapKind::Int) {
        return MaterialMap{ ReadInt64() };
    }
    if (Kind == MaterialMapKind::Bool) {
        return MaterialMap{ ReadBool() };
    }
    if (Kind == MaterialMapKind::Vec2) {
        return MaterialMap{ ReadVec2() };
    }
    if (Kind == MaterialMapKind::Vec3) {
        return MaterialMap{ ReadVec3() };
    }
    if (Kind == MaterialMapKind::Vec4) {
        return MaterialMap{ ReadVec4() };
    }
    if (Kind == MaterialMapKind::String) {
        return MaterialMap{ ReadString() };
    }
    return MaterialMap{};
}

void AssetBinaryReader::ReadModelResult(ModelResult& Result) {
    const std::uint64_t NodeCount{ ReadUint64() };
    std::vector<ModelNode*> Nodes{};
    Nodes.reserve(static_cast<std::size_t>(NodeCount));
    ReadNodes(Result, NodeCount, Nodes);
}

void AssetBinaryReader::ReadNodes(ModelResult& Result, std::uint64_t NodeCount, std::vector<ModelNode*>& Nodes) {
    for (std::uint64_t Index{ 0 }; Index < NodeCount; ++Index) {
        const std::string Name{ ReadString() };
        const std::int32_t ParentIndex{ ReadInt32() };
        ModelNode* Parent{ nullptr };
        if (ParentIndex >= 0 && ParentIndex < static_cast<std::int32_t>(Nodes.size())) {
            Parent = Nodes[static_cast<std::size_t>(ParentIndex)];
        }
        ModelNode& Node{ Result.CreateNode(Name, Parent) };
        Node.SetNodeToParent(ReadMat4());
        Node.SetGeometryToNode(ReadMat4());
        ReadVertexAttributes(Node.Vertices());
        Node.Indices() = ReadUint32Array();
        if (mFormatVersion == 1) {
            const std::vector<std::uint64_t> MaterialIndices{ ReadUint64Array() };
            std::size_t MaterialIndex{ 0 };
            if (!MaterialIndices.empty()) {
                MaterialIndex = static_cast<std::size_t>(MaterialIndices.front());
            }
            if (!Node.Indices().empty()) {
                std::vector<ModelNode::SubMesh> SubMeshes{};
                ModelNode::SubMesh SubMesh{};
                SubMesh.IndexOffset = 0;
                SubMesh.IndexCount = Node.Indices().size();
                SubMesh.MaterialIndex = MaterialIndex;
                SubMeshes.push_back(SubMesh);
                Node.SetSubMeshes(std::move(SubMeshes));
            }
        }
        else {
            Node.SetSubMeshes(ReadSubMeshes());
        }
        Nodes.push_back(&Node);
    }
}

void AssetBinaryReader::ReadVertexAttributes(VertexAttributes& Attributes) {
    Attributes.Positions = ReadVec3Array();
    Attributes.Normals = ReadVec3Array();
    for (std::size_t Index{ 0 }; Index < Attributes.TexCoords.size(); ++Index) {
        Attributes.TexCoords[Index] = ReadVec2Array();
    }
    Attributes.Colors = ReadVec4Array();
    Attributes.Tangents = ReadVec3Array();
    Attributes.Bitangents = ReadVec3Array();
    Attributes.BoneIndices = ReadUvec4Array();
    Attributes.BoneWeights = ReadVec4Array();
}

std::vector<ModelNode::SubMesh> AssetBinaryReader::ReadSubMeshes() {
    const std::uint64_t Count{ ReadUint64() };
    std::vector<ModelNode::SubMesh> SubMeshes{};
    SubMeshes.reserve(static_cast<std::size_t>(Count));
    for (std::uint64_t Index{ 0 }; Index < Count; ++Index) {
        ModelNode::SubMesh SubMesh{};
        SubMesh.IndexOffset = static_cast<std::size_t>(ReadUint64());
        SubMesh.IndexCount = static_cast<std::size_t>(ReadUint64());
        SubMesh.MaterialIndex = static_cast<std::size_t>(ReadUint64());
        SubMeshes.push_back(SubMesh);
    }
    return SubMeshes;
}

std::vector<Vec2> AssetBinaryReader::ReadVec2Array() {
    const std::uint64_t Count{ ReadUint64() };
    std::vector<Vec2> Values{};
    Values.resize(static_cast<std::size_t>(Count));
    ReadBytes(Values.data(), Values.size() * sizeof(Vec2));
    return Values;
}

std::vector<Vec3> AssetBinaryReader::ReadVec3Array() {
    const std::uint64_t Count{ ReadUint64() };
    std::vector<Vec3> Values{};
    Values.resize(static_cast<std::size_t>(Count));
    ReadBytes(Values.data(), Values.size() * sizeof(Vec3));
    return Values;
}

std::vector<Vec4> AssetBinaryReader::ReadVec4Array() {
    const std::uint64_t Count{ ReadUint64() };
    std::vector<Vec4> Values{};
    Values.resize(static_cast<std::size_t>(Count));
    ReadBytes(Values.data(), Values.size() * sizeof(Vec4));
    return Values;
}

std::vector<UVec4> AssetBinaryReader::ReadUvec4Array() {
    const std::uint64_t Count{ ReadUint64() };
    std::vector<UVec4> Values{};
    Values.resize(static_cast<std::size_t>(Count));
    ReadBytes(Values.data(), Values.size() * sizeof(UVec4));
    return Values;
}

std::vector<std::uint32_t> AssetBinaryReader::ReadUint32Array() {
    const std::uint64_t Count{ ReadUint64() };
    std::vector<std::uint32_t> Values{};
    Values.resize(static_cast<std::size_t>(Count));
    ReadBytes(Values.data(), Values.size() * sizeof(std::uint32_t));
    return Values;
}

std::vector<std::uint64_t> AssetBinaryReader::ReadUint64Array() {
    const std::uint64_t Count{ ReadUint64() };
    std::vector<std::uint64_t> Values{};
    Values.resize(static_cast<std::size_t>(Count));
    ReadBytes(Values.data(), Values.size() * sizeof(std::uint64_t));
    return Values;
}

std::string AssetBinaryReader::ReadString() {
    const std::uint64_t Length{ ReadUint64() };
    std::string Value{};
    Value.resize(static_cast<std::size_t>(Length));
    if (Length > 0) {
        ReadBytes(Value.data(), Value.size());
    }
    return Value;
}

std::uint8_t AssetBinaryReader::ReadUint8() {
    std::uint8_t Value{ 0 };
    ReadBytes(&Value, sizeof(Value));
    return Value;
}

std::uint16_t AssetBinaryReader::ReadUint16() {
    std::uint16_t Value{ 0 };
    ReadBytes(&Value, sizeof(Value));
    return Value;
}

std::uint32_t AssetBinaryReader::ReadUint32() {
    std::uint32_t Value{ 0 };
    ReadBytes(&Value, sizeof(Value));
    return Value;
}

std::uint64_t AssetBinaryReader::ReadUint64() {
    std::uint64_t Value{ 0 };
    ReadBytes(&Value, sizeof(Value));
    return Value;
}

std::int32_t AssetBinaryReader::ReadInt32() {
    std::int32_t Value{ 0 };
    ReadBytes(&Value, sizeof(Value));
    return Value;
}

std::int64_t AssetBinaryReader::ReadInt64() {
    std::int64_t Value{ 0 };
    ReadBytes(&Value, sizeof(Value));
    return Value;
}

float AssetBinaryReader::ReadFloat() {
    float Value{ 0.0f };
    ReadBytes(&Value, sizeof(Value));
    return Value;
}

bool AssetBinaryReader::ReadBool() {
    const std::uint8_t Value{ ReadUint8() };
    return Value != 0;
}

Vec2 AssetBinaryReader::ReadVec2() {
    Vec2 Value{};
    ReadBytes(&Value, sizeof(Value));
    return Value;
}

Vec3 AssetBinaryReader::ReadVec3() {
    Vec3 Value{};
    ReadBytes(&Value, sizeof(Value));
    return Value;
}

Vec4 AssetBinaryReader::ReadVec4() {
    Vec4 Value{};
    ReadBytes(&Value, sizeof(Value));
    return Value;
}

Mat4 AssetBinaryReader::ReadMat4() {
    Mat4 Value{};
    ReadBytes(&Value, sizeof(Value));
    return Value;
}

void AssetBinaryReader::ReadBytes(void* Data, std::size_t Size) {
    mStream.read(reinterpret_cast<char*>(Data), static_cast<std::streamsize>(Size));
}
