#include "GltfMeshHierarchyBuilder.h"

#include <cmath>
#include <limits>
#include <string>

namespace asset {
    GltfMeshHierarchyBuilder::GltfMeshHierarchyBuilder(ModelResult& OutResult, const std::unordered_map<const cgltf_material*, std::size_t>* MaterialLookup, GraphicsAPI Api, bool IsUvFlipEnabled)
        : mResult{ OutResult }
        , mNodeStack{}
        , mSceneRoot{ nullptr }
        , mMaterialLookup{ MaterialLookup }
        , mApi{ Api }
        , mIsUvFlipEnabled{ IsUvFlipEnabled } {
    }

    GltfMeshHierarchyBuilder::~GltfMeshHierarchyBuilder() = default;

    void GltfMeshHierarchyBuilder::OnNodeBegin(const cgltf_data& Scene, const cgltf_node& Node, const GltfNodeVisitContext& Context) {
        static_cast<void>(Scene);

        ModelNode* ParentNode{ nullptr };
        if (!mNodeStack.empty()) {
            ParentNode = mNodeStack.back();
        }
        else if (Context.mParent == nullptr) {
            if (mSceneRoot == nullptr) {
                mSceneRoot = &mResult.CreateNode(std::string{ "SceneRoot" }, nullptr);
            }

            ParentNode = mSceneRoot;
        }

        const std::string Name{ (Node.name != nullptr) ? std::string{ Node.name } : std::string{ "Unnamed" } };
        ModelNode& OutNode{ mResult.CreateNode(Name, ParentNode) };
        OutNode.SetNodeToParent(Context.mNodeToParent);
        OutNode.SetGeometryToNode(Context.mGeometryToNode);

        if (Node.mesh != nullptr) {
            AppendMesh(*Node.mesh, OutNode.Vertices(), OutNode.Indices(), OutNode.SubMeshes());
        }

        mNodeStack.push_back(&OutNode);
    }

    void GltfMeshHierarchyBuilder::OnNodeEnd(const cgltf_data& Scene, const cgltf_node& Node) {
        static_cast<void>(Scene);
        static_cast<void>(Node);

        if (mNodeStack.empty()) {
            throw AssetError{ "GltfMeshHierarchyBuilder: node stack underflow" };
        }

        mNodeStack.pop_back();
    }

    Vec3 GltfMeshHierarchyBuilder::NormalizeVec3(const Vec3& Value) {
        const float LengthValue{ std::sqrt((Value.mX * Value.mX) + (Value.mY * Value.mY) + (Value.mZ * Value.mZ)) };
        if (LengthValue <= std::numeric_limits<float>::epsilon()) {
            return Vec3{ 0.0f, 1.0f, 0.0f };
        }

        const float InverseLength{ 1.0f / LengthValue };
        return Vec3{ Value.mX * InverseLength, Value.mY * InverseLength, Value.mZ * InverseLength };
    }

    Vec3 GltfMeshHierarchyBuilder::ConvertPosition(const Vec3& Value) const {
        if (mApi == GraphicsAPI::DirectX) {
            return Vec3{ Value.mX, Value.mY, -Value.mZ };
        }

        return Value;
    }

    Vec3 GltfMeshHierarchyBuilder::ConvertDirection(const Vec3& Value) const {
        if (mApi == GraphicsAPI::DirectX) {
            return Vec3{ Value.mX, Value.mY, -Value.mZ };
        }

        return Value;
    }

    const cgltf_attribute* GltfMeshHierarchyBuilder::FindAttribute(const cgltf_primitive& Primitive, cgltf_attribute_type Type, cgltf_int Index) {
        for (cgltf_size AttributeIndex{ 0 }; AttributeIndex < Primitive.attributes_count; ++AttributeIndex) {
            const cgltf_attribute& Attribute{ Primitive.attributes[AttributeIndex] };
            if (Attribute.type == Type && Attribute.index == Index) {
                return &Attribute;
            }
        }

        return nullptr;
    }

    std::size_t GltfMeshHierarchyBuilder::GetAccessorElementCount(const cgltf_accessor& Accessor) {
        return static_cast<std::size_t>(Accessor.count);
    }

    cgltf_size GltfMeshHierarchyBuilder::GetIndexValue(const cgltf_accessor& Accessor, cgltf_size Index) {
        cgltf_uint Value{ 0U };
        const cgltf_bool IsSuccess{ cgltf_accessor_read_uint(&Accessor, Index, &Value, 1) };
        if (IsSuccess == 0) {
            throw AssetError{ "Failed to read glTF index accessor" };
        }

        return static_cast<cgltf_size>(Value);
    }

    Vec3 GltfMeshHierarchyBuilder::ReadVec3(const cgltf_accessor* Accessor, cgltf_size Index, const Vec3& DefaultValue) const {
        if (Accessor == nullptr || Index >= Accessor->count) {
            return DefaultValue;
        }

        cgltf_float Value[3]{ 0.0f, 0.0f, 0.0f };
        const cgltf_bool IsSuccess{ cgltf_accessor_read_float(Accessor, Index, Value, 3) };
        if (IsSuccess == 0) {
            return DefaultValue;
        }

        return Vec3{ Value[0], Value[1], Value[2] };
    }

    Vec2 GltfMeshHierarchyBuilder::ReadVec2(const cgltf_accessor* Accessor, cgltf_size Index, const Vec2& DefaultValue) const {
        if (Accessor == nullptr || Index >= Accessor->count) {
            return DefaultValue;
        }

        cgltf_float Value[2]{ 0.0f, 0.0f };
        const cgltf_bool IsSuccess{ cgltf_accessor_read_float(Accessor, Index, Value, 2) };
        if (IsSuccess == 0) {
            return DefaultValue;
        }

        Vec2 ResultValue{ Value[0], Value[1] };
        if (mIsUvFlipEnabled) {
            ResultValue.mY = 1.0f - ResultValue.mY;
        }

        return ResultValue;
    }

    Vec4 GltfMeshHierarchyBuilder::ReadVec4(const cgltf_accessor* Accessor, cgltf_size Index, const Vec4& DefaultValue) const {
        if (Accessor == nullptr || Index >= Accessor->count) {
            return DefaultValue;
        }

        cgltf_float Value[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
        const cgltf_bool IsSuccess{ cgltf_accessor_read_float(Accessor, Index, Value, 4) };
        if (IsSuccess == 0) {
            return DefaultValue;
        }

        return Vec4{ Value[0], Value[1], Value[2], Value[3] };
    }

    UVec4 GltfMeshHierarchyBuilder::ReadUVec4(const cgltf_accessor* Accessor, cgltf_size Index, const UVec4& DefaultValue) const {
        if (Accessor == nullptr || Index >= Accessor->count) {
            return DefaultValue;
        }

        cgltf_uint Value[4]{ 0U, 0U, 0U, 0U };
        const cgltf_bool IsSuccess{ cgltf_accessor_read_uint(Accessor, Index, Value, 4) };
        if (IsSuccess == 0) {
            return DefaultValue;
        }

        return UVec4{ Value[0], Value[1], Value[2], Value[3] };
    }

    std::size_t GltfMeshHierarchyBuilder::ResolveMaterialGroupItemIndex(const cgltf_primitive& Primitive) const {
        if (mMaterialLookup == nullptr || Primitive.material == nullptr) {
            return 0;
        }

        const auto Iterator{ mMaterialLookup->find(Primitive.material) };
        if (Iterator == mMaterialLookup->end()) {
            return 0;
        }

        return Iterator->second;
    }

    GltfMeshHierarchyBuilder::PrimitiveVertexData GltfMeshHierarchyBuilder::BuildPrimitiveVertexData(const cgltf_primitive& Primitive) const {
        const cgltf_attribute* const PositionAttribute{ FindAttribute(Primitive, cgltf_attribute_type_position, 0) };
        if (PositionAttribute == nullptr || PositionAttribute->data == nullptr) {
            throw AssetError{ "glTF primitive does not have POSITION attribute" };
        }

        const cgltf_attribute* const NormalAttribute{ FindAttribute(Primitive, cgltf_attribute_type_normal, 0) };
        const cgltf_attribute* const TangentAttribute{ FindAttribute(Primitive, cgltf_attribute_type_tangent, 0) };
        const cgltf_attribute* const ColorAttribute{ FindAttribute(Primitive, cgltf_attribute_type_color, 0) };
        const cgltf_attribute* const JointsAttribute{ FindAttribute(Primitive, cgltf_attribute_type_joints, 0) };
        const cgltf_attribute* const WeightsAttribute{ FindAttribute(Primitive, cgltf_attribute_type_weights, 0) };
        const cgltf_attribute* const TexCoord0Attribute{ FindAttribute(Primitive, cgltf_attribute_type_texcoord, 0) };
        const cgltf_attribute* const TexCoord1Attribute{ FindAttribute(Primitive, cgltf_attribute_type_texcoord, 1) };
        const cgltf_attribute* const TexCoord2Attribute{ FindAttribute(Primitive, cgltf_attribute_type_texcoord, 2) };
        const cgltf_attribute* const TexCoord3Attribute{ FindAttribute(Primitive, cgltf_attribute_type_texcoord, 3) };

        const std::size_t VertexCount{ GetAccessorElementCount(*PositionAttribute->data) };
        PrimitiveVertexData PrimitiveData{};
        PrimitiveData.mVertices.Reserve(VertexCount);

        for (std::size_t VertexIndex{ 0 }; VertexIndex < VertexCount; ++VertexIndex) {
            const cgltf_size AccessorIndex{ static_cast<cgltf_size>(VertexIndex) };
            const Vec3 Position{ ConvertPosition(ReadVec3(PositionAttribute->data, AccessorIndex, Vec3{ 0.0f, 0.0f, 0.0f })) };
            const Vec3 Normal{ NormalizeVec3(ConvertDirection(ReadVec3(NormalAttribute != nullptr ? NormalAttribute->data : nullptr, AccessorIndex, Vec3{ 0.0f, 1.0f, 0.0f }))) };
            const Vec4 TangentValue{ ReadVec4(TangentAttribute != nullptr ? TangentAttribute->data : nullptr, AccessorIndex, Vec4{ 0.0f, 0.0f, 0.0f, 1.0f }) };
            const Vec4 Color{ ReadVec4(ColorAttribute != nullptr ? ColorAttribute->data : nullptr, AccessorIndex, Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }) };
            const Vec3 Tangent{ NormalizeVec3(ConvertDirection(Vec3{ TangentValue.mX, TangentValue.mY, TangentValue.mZ })) };
            const Vec3 Bitangent{ 0.0f, 0.0f, 0.0f };
            const UVec4 BoneIndices{ ReadUVec4(JointsAttribute != nullptr ? JointsAttribute->data : nullptr, AccessorIndex, UVec4{ 0U, 0U, 0U, 0U }) };
            const Vec4 BoneWeights{ ReadVec4(WeightsAttribute != nullptr ? WeightsAttribute->data : nullptr, AccessorIndex, Vec4{ 0.0f, 0.0f, 0.0f, 0.0f }) };

            PrimitiveData.mVertices.Positions.push_back(Position);
            PrimitiveData.mVertices.Normals.push_back(Normal);
            PrimitiveData.mVertices.Colors.push_back(Color);
            PrimitiveData.mVertices.Tangents.push_back(Tangent);
            PrimitiveData.mVertices.Bitangents.push_back(Bitangent);
            PrimitiveData.mVertices.BoneIndices.push_back(BoneIndices);
            PrimitiveData.mVertices.BoneWeights.push_back(BoneWeights);
            PrimitiveData.mVertices.TexCoords[0].push_back(ReadVec2(TexCoord0Attribute != nullptr ? TexCoord0Attribute->data : nullptr, AccessorIndex, Vec2{ 0.0f, 0.0f }));
            PrimitiveData.mVertices.TexCoords[1].push_back(ReadVec2(TexCoord1Attribute != nullptr ? TexCoord1Attribute->data : nullptr, AccessorIndex, Vec2{ 0.0f, 0.0f }));
            PrimitiveData.mVertices.TexCoords[2].push_back(ReadVec2(TexCoord2Attribute != nullptr ? TexCoord2Attribute->data : nullptr, AccessorIndex, Vec2{ 0.0f, 0.0f }));
            PrimitiveData.mVertices.TexCoords[3].push_back(ReadVec2(TexCoord3Attribute != nullptr ? TexCoord3Attribute->data : nullptr, AccessorIndex, Vec2{ 0.0f, 0.0f }));
        }

        if (Primitive.indices != nullptr) {
            const std::size_t IndexCount{ GetAccessorElementCount(*Primitive.indices) };
            PrimitiveData.mIndices.reserve(IndexCount);
            for (std::size_t Index{ 0 }; Index < IndexCount; ++Index) {
                PrimitiveData.mIndices.push_back(static_cast<std::uint32_t>(GetIndexValue(*Primitive.indices, static_cast<cgltf_size>(Index))));
            }
        }
        else {
            PrimitiveData.mIndices.reserve(VertexCount);
            for (std::size_t Index{ 0 }; Index < VertexCount; ++Index) {
                PrimitiveData.mIndices.push_back(static_cast<std::uint32_t>(Index));
            }
        }

        return PrimitiveData;
    }

    void GltfMeshHierarchyBuilder::AppendPrimitive(const cgltf_primitive& Primitive, VertexAttributes& OutVertices, std::vector<std::uint32_t>& OutIndices, std::vector<ModelNode::SubMesh>& OutSubMeshes) const {
        if (Primitive.type != cgltf_primitive_type_triangles) {
            return;
        }

        PrimitiveVertexData PrimitiveData{ BuildPrimitiveVertexData(Primitive) };
        const std::uint32_t VertexOffset{ static_cast<std::uint32_t>(OutVertices.VertexCount()) };
        const std::size_t SubMeshIndexOffset{ OutIndices.size() };

        OutVertices.Positions.insert(OutVertices.Positions.end(), PrimitiveData.mVertices.Positions.begin(), PrimitiveData.mVertices.Positions.end());
        OutVertices.Normals.insert(OutVertices.Normals.end(), PrimitiveData.mVertices.Normals.begin(), PrimitiveData.mVertices.Normals.end());
        OutVertices.Colors.insert(OutVertices.Colors.end(), PrimitiveData.mVertices.Colors.begin(), PrimitiveData.mVertices.Colors.end());
        OutVertices.Tangents.insert(OutVertices.Tangents.end(), PrimitiveData.mVertices.Tangents.begin(), PrimitiveData.mVertices.Tangents.end());
        OutVertices.Bitangents.insert(OutVertices.Bitangents.end(), PrimitiveData.mVertices.Bitangents.begin(), PrimitiveData.mVertices.Bitangents.end());
        OutVertices.BoneIndices.insert(OutVertices.BoneIndices.end(), PrimitiveData.mVertices.BoneIndices.begin(), PrimitiveData.mVertices.BoneIndices.end());
        OutVertices.BoneWeights.insert(OutVertices.BoneWeights.end(), PrimitiveData.mVertices.BoneWeights.begin(), PrimitiveData.mVertices.BoneWeights.end());

        for (std::size_t TexCoordIndex{ 0 }; TexCoordIndex < OutVertices.TexCoords.size(); ++TexCoordIndex) {
            OutVertices.TexCoords[TexCoordIndex].insert(OutVertices.TexCoords[TexCoordIndex].end(), PrimitiveData.mVertices.TexCoords[TexCoordIndex].begin(), PrimitiveData.mVertices.TexCoords[TexCoordIndex].end());
        }

        OutIndices.reserve(OutIndices.size() + PrimitiveData.mIndices.size());
        for (std::size_t Index{ 0 }; Index + 2 < PrimitiveData.mIndices.size(); Index += 3) {
            const std::uint32_t Index0{ PrimitiveData.mIndices[Index + 0] + VertexOffset };
            const std::uint32_t Index1{ PrimitiveData.mIndices[Index + 1] + VertexOffset };
            const std::uint32_t Index2{ PrimitiveData.mIndices[Index + 2] + VertexOffset };
            OutIndices.push_back(Index0);
            OutIndices.push_back(Index2);
            OutIndices.push_back(Index1);
        }

        ModelNode::SubMesh SubMesh{};
        SubMesh.IndexOffset = SubMeshIndexOffset;
        SubMesh.IndexCount = OutIndices.size() - SubMeshIndexOffset;
        SubMesh.MaterialGroupItemIndex = ResolveMaterialGroupItemIndex(Primitive);
        OutSubMeshes.push_back(SubMesh);
    }

    void GltfMeshHierarchyBuilder::AppendMesh(const cgltf_mesh& Mesh, VertexAttributes& OutVertices, std::vector<std::uint32_t>& OutIndices, std::vector<ModelNode::SubMesh>& OutSubMeshes) const {
        for (cgltf_size PrimitiveIndex{ 0 }; PrimitiveIndex < Mesh.primitives_count; ++PrimitiveIndex) {
            AppendPrimitive(Mesh.primitives[PrimitiveIndex], OutVertices, OutIndices, OutSubMeshes);
        }
    }
}
