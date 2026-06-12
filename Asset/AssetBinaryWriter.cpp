#include "AssetBinaryWriter.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_map>
#include <vector>

using namespace asset;

namespace {
    constexpr std::uint32_t FormatVersion{ 10 };
    constexpr char FormatMagic[4]{ 'F', 'B', 'X', 'B' };

    bool TryBuildObbFromPoints(const std::vector<DirectX::XMFLOAT3>& PositionPoints, DirectX::BoundingOrientedBox& OutBoundingBox) {
        if (PositionPoints.empty()) {
            return false;
        }

        DirectX::BoundingBox AxisAlignedBoundingBox{};
        DirectX::BoundingBox::CreateFromPoints(AxisAlignedBoundingBox, PositionPoints.size(), PositionPoints.data(), sizeof(DirectX::XMFLOAT3));
        DirectX::BoundingOrientedBox::CreateFromBoundingBox(OutBoundingBox, AxisAlignedBoundingBox);
        return true;
    }

    bool TryBuildStaticNodeBoundingBox(const ModelNode& Node, DirectX::BoundingOrientedBox& OutBoundingBox) {
        const std::vector<Vec3>& Positions{ Node.Vertices().Positions };
        std::vector<DirectX::XMFLOAT3> PositionPoints{};
        PositionPoints.reserve(Positions.size());

        for (const Vec3& Position : Positions) {
            PositionPoints.push_back(DirectX::XMFLOAT3{ Position.x, Position.y, Position.z });
        }

        return TryBuildObbFromPoints(PositionPoints, OutBoundingBox);
    }

    std::unordered_map<const ModelNode*, DirectX::BoundingOrientedBox> BuildNodeBoundingBoxes(const std::vector<const ModelNode*>& Nodes) {
        std::unordered_map<const ModelNode*, DirectX::BoundingOrientedBox> NodeBoundingBoxes{};
        NodeBoundingBoxes.reserve(Nodes.size());
        std::unordered_map<std::string, const ModelNode*> NodeByName{};
        NodeByName.reserve(Nodes.size());

        for (const ModelNode* Node : Nodes) {
            NodeByName[Node->GetName()] = Node;
        }

        std::unordered_map<const ModelNode*, std::vector<DirectX::XMFLOAT3>> BoneNodePositionMap{};
        BoneNodePositionMap.reserve(Nodes.size());

        for (const ModelNode* Node : Nodes) {
            if (Node->IsSkinnedMesh() == false) {
                DirectX::BoundingOrientedBox BoundingBox{};
                const bool IsBuilt{ TryBuildStaticNodeBoundingBox(*Node, BoundingBox) };
                if (IsBuilt == true) {
                    NodeBoundingBoxes[Node] = BoundingBox;
                }
                continue;
            }

            const std::vector<Vec3>& Positions{ Node->Vertices().Positions };
            const std::vector<UVec4>& BoneIndices{ Node->Vertices().BoneIndices };
            const std::vector<Vec4>& BoneWeights{ Node->Vertices().BoneWeights };
            const std::vector<ModelBoneInfo>& BoneInfos{ Node->BoneInfos() };
            if (Positions.empty() == true || BoneIndices.size() != Positions.size() || BoneWeights.size() != Positions.size() || BoneInfos.empty() == true) {
                continue;
            }

            for (std::size_t VertexIndex{ 0 }; VertexIndex < Positions.size(); ++VertexIndex) {
                const UVec4& VertexBoneIndices{ BoneIndices[VertexIndex] };
                const Vec4& VertexBoneWeights{ BoneWeights[VertexIndex] };
                const std::array<float, 4> WeightValues{ VertexBoneWeights.x, VertexBoneWeights.y, VertexBoneWeights.z, VertexBoneWeights.w };
                std::size_t DominantWeightIndex{ 0 };
                float DominantWeightValue{ WeightValues[0] };

                for (std::size_t WeightIndex{ 1 }; WeightIndex < WeightValues.size(); ++WeightIndex) {
                    if (WeightValues[WeightIndex] > DominantWeightValue) {
                        DominantWeightValue = WeightValues[WeightIndex];
                        DominantWeightIndex = WeightIndex;
                    }
                }

                const std::uint32_t JointArrayIndex{ VertexBoneIndices[DominantWeightIndex] };
                const auto BoneInfoIter{ std::find_if(BoneInfos.begin(), BoneInfos.end(), [JointArrayIndex](const ModelBoneInfo& BoneInfo) {
                    return BoneInfo.JointArrayIndex == JointArrayIndex;
                }) };
                if (BoneInfoIter == BoneInfos.end()) {
                    continue;
                }

                const auto NodeIter{ NodeByName.find(BoneInfoIter->BoneName) };
                if (NodeIter == NodeByName.end()) {
                    continue;
                }

                const Vec3& Position{ Positions[VertexIndex] };
                const SimpleMath::Vector3 PositionVector{ Position.x, Position.y, Position.z };
                const SimpleMath::Vector3 BoneLocalPosition{ SimpleMath::Vector3::Transform(PositionVector, BoneInfoIter->InverseBindMatrix) };
                BoneNodePositionMap[NodeIter->second].push_back(DirectX::XMFLOAT3{ BoneLocalPosition.x, BoneLocalPosition.y, BoneLocalPosition.z });
            }
        }

        for (const auto& [BoneNode, BonePositions] : BoneNodePositionMap) {
            DirectX::BoundingOrientedBox BoneBoundingBox{};
            const bool IsBuilt{ TryBuildObbFromPoints(BonePositions, BoneBoundingBox) };
            if (IsBuilt == false) {
                continue;
            }

            NodeBoundingBoxes[BoneNode] = BoneBoundingBox;
        }

        return NodeBoundingBoxes;
    }
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

    const std::string UnifiedSkinBoneRootNodeName{ ResolveUnifiedSkinBoneRootNodeName(Nodes) };
    const std::unordered_map<const ModelNode*, DirectX::BoundingOrientedBox> NodeBoundingBoxes{ BuildNodeBoundingBoxes(Nodes) };
    WriteUint64(static_cast<std::uint64_t>(Nodes.size()));
    WriteString(UnifiedSkinBoneRootNodeName);
    WriteNodes(Nodes, NodeIndices, NodeBoundingBoxes);
}

void AssetBinaryWriter::WriteNodes(const std::vector<const ModelNode*>& Nodes, const std::unordered_map<const ModelNode*, std::uint32_t>& NodeIndices, const std::unordered_map<const ModelNode*, DirectX::BoundingOrientedBox>& NodeBoundingBoxes) {
    for (const ModelNode* Node : Nodes) {
        WriteNode(*Node, NodeIndices, NodeBoundingBoxes);
    }
}

void AssetBinaryWriter::WriteNode(const ModelNode& Node, const std::unordered_map<const ModelNode*, std::uint32_t>& NodeIndices, const std::unordered_map<const ModelNode*, DirectX::BoundingOrientedBox>& NodeBoundingBoxes) {
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
    WriteBoundingBox(Node, NodeBoundingBoxes);
    WriteVertexAttributes(Node.Vertices());
    WriteUint32Array(Node.Indices());
    WriteSubMeshes(Node.GetSubMeshes());
}

std::string AssetBinaryWriter::ResolveUnifiedSkinBoneRootNodeName(const std::vector<const ModelNode*>& Nodes) const {
    std::string UnifiedSkinBoneRootNodeName{};
    for (const ModelNode* Node : Nodes) {
        if (Node->IsSkinnedMesh() == false) {
            continue;
        }

        UnifiedSkinBoneRootNodeName = Node->GetSkinBoneRootNodeName();
        break;
    }

    return UnifiedSkinBoneRootNodeName;
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

void AssetBinaryWriter::WriteBoundingBox(const ModelNode& Node, const std::unordered_map<const ModelNode*, DirectX::BoundingOrientedBox>& NodeBoundingBoxes) {
    const auto BoundingBoxIter{ NodeBoundingBoxes.find(&Node) };
    const bool IsBuilt{ BoundingBoxIter != NodeBoundingBoxes.end() };
    WriteBool(IsBuilt);
    if (IsBuilt == true) {
        WriteBoundingOrientedBox(BoundingBoxIter->second);
    }
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

void AssetBinaryWriter::WriteBoundingOrientedBox(const DirectX::BoundingOrientedBox& Value) {
    WriteBytes(&Value, sizeof(Value));
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
