#include "MeshHierarchyBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace asset;

namespace {
    Vec3 NormalizeVec3(const Vec3& Value) {
        const float LengthValue{ std::sqrt((Value.mX * Value.mX) + (Value.mY * Value.mY) + (Value.mZ * Value.mZ)) };
        if (LengthValue <= 0.0f) {
            return Vec3{ 0.0f, 1.0f, 0.0f };
        }
        const float InvLength{ 1.0f / LengthValue };
        return Vec3{ Value.mX * InvLength, Value.mY * InvLength, Value.mZ * InvLength };
    }
}

MeshHierarchyBuilder::MeshHierarchyBuilder(ModelResult& OutResult, const std::unordered_map<const ufbx_material*, std::size_t>* MaterialLookup)
    : mResult{ OutResult }
    , mMaterialLookup{ MaterialLookup } {
}

MeshHierarchyBuilder::~MeshHierarchyBuilder() = default;

void MeshHierarchyBuilder::OnNodeBegin(const ufbx_scene& Scene, const ufbx_node& Node, const NodeVisitContext& Context) {
    static_cast<void>(Scene);
    ModelNode* ParentNode{ nullptr };
    if (!mNodeStack.empty()) {
        ParentNode = mNodeStack.back();
    }

    const std::string Name{ (Node.name.data != nullptr) ? std::string{ Node.name.data, Node.name.length } : std::string{ "Unnamed" } };

    ModelNode& OutNode{ mResult.CreateNode(Name, ParentNode) };
    OutNode.SetNodeToParent(Context.mNodeToParent);
    OutNode.SetGeometryToNode(Context.mGeometryToNode);
    if (Node.mesh != nullptr) {
        AppendIndexedMeshUfbx(Node, *Node.mesh, OutNode.Vertices(), OutNode.Indices(), OutNode.SubMeshes());
    }
    mNodeStack.push_back(&OutNode);
}

void MeshHierarchyBuilder::OnNodeEnd(const ufbx_scene& Scene, const ufbx_node& Node) {
    static_cast<void>(Scene);
    static_cast<void>(Node);
    if (!mNodeStack.empty()) {
        mNodeStack.pop_back();
        return;
    }
    throw AssetError{ "MeshHierarchyBuilder: node stack underflow" };
}

Vec3 MeshHierarchyBuilder::ToVec3(const ufbx_vec3& Value) {
    return Vec3{ static_cast<float>(Value.x), static_cast<float>(Value.y), static_cast<float>(Value.z) };
}

Vec2 MeshHierarchyBuilder::ToVec2(const ufbx_vec2& Value) {
    return Vec2{ static_cast<float>(Value.x), static_cast<float>(Value.y) };
}

Vec4 MeshHierarchyBuilder::ToVec4(const ufbx_vec4& Value) {
    return Vec4{ static_cast<float>(Value.x), static_cast<float>(Value.y), static_cast<float>(Value.z), static_cast<float>(Value.w) };
}

Vec3 MeshHierarchyBuilder::ReadPosition(const ufbx_mesh& Mesh, std::uint32_t Index) const {
    const ufbx_vec3 Value{ ufbx_get_vertex_vec3(&Mesh.vertex_position, Index) };
    return ToVec3(Value);
}

Vec3 MeshHierarchyBuilder::ReadNormal(const ufbx_mesh& Mesh, std::uint32_t Index) const {
    if (Mesh.vertex_normal.exists) {
        const ufbx_vec3 Value{ ufbx_get_vertex_vec3(&Mesh.vertex_normal, Index) };
        return NormalizeVec3(ToVec3(Value));
    }
    return Vec3{ 0.0f, 1.0f, 0.0f };
}

Vec2 MeshHierarchyBuilder::ReadTexCoord(const ufbx_mesh& Mesh, std::size_t SetIndex, std::uint32_t Index) const {
    if (SetIndex == 0) {
        if (Mesh.vertex_uv.exists) {
            const ufbx_vec2 Value{ ufbx_get_vertex_vec2(&Mesh.vertex_uv, Index) };
            return ToVec2(Value);
        }
        return Vec2{ 0.0f, 0.0f };
    }

    if (SetIndex < Mesh.uv_sets.count) {
        const ufbx_uv_set& Set{ Mesh.uv_sets.data[SetIndex] };
        if (Set.vertex_uv.exists) {
            const ufbx_vec2 Value{ ufbx_get_vertex_vec2(&Set.vertex_uv, Index) };
            return ToVec2(Value);
        }
    }
    return Vec2{ 0.0f, 0.0f };
}

Vec4 MeshHierarchyBuilder::ReadColor(const ufbx_mesh& Mesh, std::uint32_t Index) const {
    if (Mesh.vertex_color.exists) {
        const ufbx_vec4 Value{ ufbx_get_vertex_vec4(&Mesh.vertex_color, Index) };
        return ToVec4(Value);
    }
    return Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
}

Vec3 MeshHierarchyBuilder::ReadTangent(const ufbx_mesh& Mesh, std::uint32_t Index) const {
    if (Mesh.vertex_tangent.exists) {
        const ufbx_vec3 Value{ ufbx_get_vertex_vec3(&Mesh.vertex_tangent, Index) };
        return ToVec3(Value);
    }
    return Vec3{ 0.0f, 0.0f, 0.0f };
}

Vec3 MeshHierarchyBuilder::ReadBitangent(const ufbx_mesh& Mesh, std::uint32_t Index) const {
    if (Mesh.vertex_bitangent.exists) {
        const ufbx_vec3 Value{ ufbx_get_vertex_vec3(&Mesh.vertex_bitangent, Index) };
        return ToVec3(Value);
    }
    return Vec3{ 0.0f, 0.0f, 0.0f };
}

void MeshHierarchyBuilder::ReadBoneData(const ufbx_mesh& Mesh, std::uint32_t CornerIndex, UVec4& OutIndices, Vec4& OutWeights) const {
    std::uint32_t Indices[4]{ 0, 0, 0, 0 };
    float Weights[4]{ 0.0f, 0.0f, 0.0f, 0.0f };

    if (Mesh.skin_deformers.count == 0) {
        OutIndices = UVec4{ Indices[0], Indices[1], Indices[2], Indices[3] };
        OutWeights = Vec4{ Weights[0], Weights[1], Weights[2], Weights[3] };
        return;
    }

    const ufbx_skin_deformer* Skin{ Mesh.skin_deformers.data[0] };
    if (Skin == nullptr) {
        OutIndices = UVec4{ Indices[0], Indices[1], Indices[2], Indices[3] };
        OutWeights = Vec4{ Weights[0], Weights[1], Weights[2], Weights[3] };
        return;
    }

    if (CornerIndex >= Mesh.vertex_indices.count) {
        OutIndices = UVec4{ Indices[0], Indices[1], Indices[2], Indices[3] };
        OutWeights = Vec4{ Weights[0], Weights[1], Weights[2], Weights[3] };
        return;
    }

    const std::uint32_t VertexIndex{ Mesh.vertex_indices.data[CornerIndex] };
    if (VertexIndex >= Skin->vertices.count) {
        OutIndices = UVec4{ Indices[0], Indices[1], Indices[2], Indices[3] };
        OutWeights = Vec4{ Weights[0], Weights[1], Weights[2], Weights[3] };
        return;
    }

    const ufbx_skin_vertex& SkinVertex{ Skin->vertices.data[VertexIndex] };
    const std::size_t WeightCount{ std::min<std::size_t>(SkinVertex.num_weights, 4) };
    if (WeightCount == 0) {
        OutIndices = UVec4{ Indices[0], Indices[1], Indices[2], Indices[3] };
        OutWeights = Vec4{ Weights[0], Weights[1], Weights[2], Weights[3] };
        return;
    }

    float WeightSum{ 0.0f };
    for (std::size_t Index{ 0 }; Index < WeightCount; ++Index) {
        const std::size_t WeightIndex{ SkinVertex.weight_begin + Index };
        if (WeightIndex >= Skin->weights.count) {
            break;
        }
        const ufbx_skin_weight& Weight{ Skin->weights.data[WeightIndex] };
        Indices[Index] = Weight.cluster_index;
        Weights[Index] = static_cast<float>(Weight.weight);
        WeightSum += Weights[Index];
    }

    if (WeightSum > 0.0f) {
        for (std::size_t Index{ 0 }; Index < WeightCount; ++Index) {
            Weights[Index] = Weights[Index] / WeightSum;
        }
    }

    OutIndices = UVec4{ Indices[0], Indices[1], Indices[2], Indices[3] };
    OutWeights = Vec4{ Weights[0], Weights[1], Weights[2], Weights[3] };
}

std::size_t MeshHierarchyBuilder::ResolveMaterialIndex(const ufbx_node& Node, const ufbx_mesh& Mesh, std::size_t FaceIndex) const {
    std::size_t MaterialIndex{ 0 };
    if (mMaterialLookup == nullptr) {
        return MaterialIndex;
    }
    if (Node.materials.count == 0) {
        return MaterialIndex;
    }
    std::size_t MaterialSlot{ 0 };
    if (FaceIndex < Mesh.face_material.count) {
        MaterialSlot = Mesh.face_material.data[FaceIndex];
    }
    if (MaterialSlot >= Node.materials.count) {
        MaterialSlot = 0;
    }
    const ufbx_material* MaterialData{ Node.materials.data[MaterialSlot] };
    if (MaterialData == nullptr) {
        return MaterialIndex;
    }
    auto Lookup{ mMaterialLookup->find(MaterialData) };
    if (Lookup == mMaterialLookup->end()) {
        return MaterialIndex;
    }
    return Lookup->second;
}

MeshHierarchyBuilder::PackedVertex MeshHierarchyBuilder::MakePackedVertex(const ufbx_mesh& Mesh, std::uint32_t CornerIndex) const {
    PackedVertex Packed{};
    const Vec3 Position{ ReadPosition(Mesh, CornerIndex) };
    const Vec3 Normal{ ReadNormal(Mesh, CornerIndex) };
    const Vec4 Color{ ReadColor(Mesh, CornerIndex) };
    const Vec3 Tangent{ ReadTangent(Mesh, CornerIndex) };
    const Vec3 Bitangent{ ReadBitangent(Mesh, CornerIndex) };
    UVec4 BoneIndices{};
    Vec4 BoneWeights{};
    ReadBoneData(Mesh, CornerIndex, BoneIndices, BoneWeights);

    Packed.Position[0] = Position.mX;
    Packed.Position[1] = Position.mY;
    Packed.Position[2] = Position.mZ;
    Packed.Normal[0] = Normal.mX;
    Packed.Normal[1] = Normal.mY;
    Packed.Normal[2] = Normal.mZ;
    Packed.Color[0] = Color.mX;
    Packed.Color[1] = Color.mY;
    Packed.Color[2] = Color.mZ;
    Packed.Color[3] = Color.mW;
    Packed.Tangent[0] = Tangent.mX;
    Packed.Tangent[1] = Tangent.mY;
    Packed.Tangent[2] = Tangent.mZ;
    Packed.Bitangent[0] = Bitangent.mX;
    Packed.Bitangent[1] = Bitangent.mY;
    Packed.Bitangent[2] = Bitangent.mZ;
    Packed.BoneIndices[0] = BoneIndices.mX;
    Packed.BoneIndices[1] = BoneIndices.mY;
    Packed.BoneIndices[2] = BoneIndices.mZ;
    Packed.BoneIndices[3] = BoneIndices.mW;
    Packed.BoneWeights[0] = BoneWeights.mX;
    Packed.BoneWeights[1] = BoneWeights.mY;
    Packed.BoneWeights[2] = BoneWeights.mZ;
    Packed.BoneWeights[3] = BoneWeights.mW;

    for (std::size_t SetIndex{ 0 }; SetIndex < 4; ++SetIndex) {
        const Vec2 TexCoord{ ReadTexCoord(Mesh, SetIndex, CornerIndex) };
        Packed.TexCoord[SetIndex][0] = TexCoord.mX;
        Packed.TexCoord[SetIndex][1] = TexCoord.mY;
    }

    return Packed;
}

void MeshHierarchyBuilder::AppendIndexedMeshUfbx(const ufbx_node& Node, const ufbx_mesh& Mesh, VertexAttributes& OutVertices, std::vector<std::uint32_t>& OutIndices, std::vector<ModelNode::SubMesh>& OutSubMeshes) const {
    const std::size_t NumCorners{ Mesh.num_indices };
    if (NumCorners == 0) {
        return;
    }

    std::vector<PackedVertex> CornerVertices{};
    CornerVertices.resize(NumCorners);
    for (std::size_t CornerIndex{ 0 }; CornerIndex < NumCorners; ++CornerIndex) {
        CornerVertices[CornerIndex] = MakePackedVertex(Mesh, static_cast<std::uint32_t>(CornerIndex));
    }

    std::vector<std::uint32_t> Remap{};
    Remap.resize(NumCorners);
    ufbx_vertex_stream Stream{};
    Stream.data = CornerVertices.data();
    Stream.vertex_count = NumCorners;
    Stream.vertex_size = sizeof(PackedVertex);
    ufbx_error GenErr{};
    const std::size_t UniqueVertexCount{ ufbx_generate_indices(&Stream, 1, Remap.data(), NumCorners, nullptr, &GenErr) };
    if (UniqueVertexCount == 0) {
        const std::string Desc{ (GenErr.description.data != nullptr) ? std::string{ GenErr.description.data, GenErr.description.length } : std::string{} };
        if (!Desc.empty()) {
            throw AssetError{ std::string{ "ufbx_generate_indices failed: " } + Desc };
        }
        throw AssetError{ "ufbx_generate_indices failed: unknown error" };
    }

    OutVertices.Reserve(OutVertices.VertexCount() + UniqueVertexCount);

    for (std::size_t VertexIndex{ 0 }; VertexIndex < UniqueVertexCount; ++VertexIndex) {
        const PackedVertex& Packed{ CornerVertices[VertexIndex] };
        OutVertices.Positions.push_back(Vec3{ Packed.Position[0], Packed.Position[1], Packed.Position[2] });
        OutVertices.Normals.push_back(Vec3{ Packed.Normal[0], Packed.Normal[1], Packed.Normal[2] });
        OutVertices.Colors.push_back(Vec4{ Packed.Color[0], Packed.Color[1], Packed.Color[2], Packed.Color[3] });
        OutVertices.Tangents.push_back(Vec3{ Packed.Tangent[0], Packed.Tangent[1], Packed.Tangent[2] });
        OutVertices.Bitangents.push_back(Vec3{ Packed.Bitangent[0], Packed.Bitangent[1], Packed.Bitangent[2] });
        OutVertices.BoneIndices.push_back(UVec4{ Packed.BoneIndices[0], Packed.BoneIndices[1], Packed.BoneIndices[2], Packed.BoneIndices[3] });
        OutVertices.BoneWeights.push_back(Vec4{ Packed.BoneWeights[0], Packed.BoneWeights[1], Packed.BoneWeights[2], Packed.BoneWeights[3] });
        for (std::size_t SetIndex{ 0 }; SetIndex < 4; ++SetIndex) {
            OutVertices.TexCoords[SetIndex].push_back(Vec2{ Packed.TexCoord[SetIndex][0], Packed.TexCoord[SetIndex][1] });
        }
    }

    std::map<std::size_t, std::vector<std::uint32_t>> MaterialBatches{};
    std::vector<std::uint32_t> TriCorners{};
    TriCorners.resize(static_cast<std::size_t>(Mesh.max_face_triangles) * 3);
    for (std::size_t FaceIndex{ 0 }; FaceIndex < Mesh.faces.count; ++FaceIndex) {
        const ufbx_face Face{ Mesh.faces.data[FaceIndex] };
        if (Face.num_indices < 3) {
            continue;
        }
        const std::size_t MaterialIndex{ ResolveMaterialIndex(Node, Mesh, FaceIndex) };
        std::vector<std::uint32_t>& MaterialIndices{ MaterialBatches[MaterialIndex] };
        const std::uint32_t NumTris{ ufbx_triangulate_face(TriCorners.data(), TriCorners.size(), &Mesh, Face) };
        for (std::uint32_t TriIndex{ 0 }; TriIndex < NumTris; ++TriIndex) {
            const std::uint32_t Corner0{ TriCorners[TriIndex * 3 + 0] };
            const std::uint32_t Corner1{ TriCorners[TriIndex * 3 + 1] };
            const std::uint32_t Corner2{ TriCorners[TriIndex * 3 + 2] };
            MaterialIndices.push_back(Remap[Corner0]);
            MaterialIndices.push_back(Remap[Corner1]);
            MaterialIndices.push_back(Remap[Corner2]);
        }
    }

    std::size_t IndexOffset{ 0 };
    for (const auto& Pair : MaterialBatches) {
        const std::vector<std::uint32_t>& BatchIndices{ Pair.second };
        if (BatchIndices.empty()) {
            continue;
        }
        OutIndices.insert(OutIndices.end(), BatchIndices.begin(), BatchIndices.end());
        ModelNode::SubMesh SubMesh{};
        SubMesh.IndexOffset = IndexOffset;
        SubMesh.IndexCount = BatchIndices.size();
        SubMesh.MaterialIndex = Pair.first;
        OutSubMeshes.push_back(SubMesh);
        IndexOffset += BatchIndices.size();
    }
}
