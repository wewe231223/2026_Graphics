#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "GltfSceneVisitor.h"
#include "ModelResult.h"

namespace asset {
    class GltfMeshHierarchyBuilder final : public IGltfSceneNodeVisitor {
    public:
        explicit GltfMeshHierarchyBuilder(ModelResult& OutResult, const std::unordered_map<const cgltf_material*, std::size_t>* MaterialLookup, GraphicsAPI Api, bool IsUvFlipEnabled);
        ~GltfMeshHierarchyBuilder();

        GltfMeshHierarchyBuilder(const GltfMeshHierarchyBuilder& Other) = delete;
        GltfMeshHierarchyBuilder& operator=(const GltfMeshHierarchyBuilder& Other) = delete;
        GltfMeshHierarchyBuilder(GltfMeshHierarchyBuilder&& Other) = delete;
        GltfMeshHierarchyBuilder& operator=(GltfMeshHierarchyBuilder&& Other) = delete;

    public:
        void OnNodeBegin(const cgltf_data& Scene, const cgltf_node& Node, const GltfNodeVisitContext& Context) override;
        void OnNodeEnd(const cgltf_data& Scene, const cgltf_node& Node) override;

    private:
        struct PrimitiveVertexData final {
        public:
            VertexAttributes mVertices{};
            std::vector<std::uint32_t> mIndices{};
        };

        static Vec3 NormalizeVec3(const Vec3& Value);
        Vec3 ConvertPosition(const Vec3& Value) const;
        Vec3 ConvertDirection(const Vec3& Value) const;
        static const cgltf_attribute* FindAttribute(const cgltf_primitive& Primitive, cgltf_attribute_type Type, cgltf_int Index);
        static std::size_t GetAccessorElementCount(const cgltf_accessor& Accessor);
        static cgltf_size GetIndexValue(const cgltf_accessor& Accessor, cgltf_size Index);

        Vec3 ReadVec3(const cgltf_accessor* Accessor, cgltf_size Index, const Vec3& DefaultValue) const;
        Vec2 ReadVec2(const cgltf_accessor* Accessor, cgltf_size Index, const Vec2& DefaultValue) const;
        Vec4 ReadVec4(const cgltf_accessor* Accessor, cgltf_size Index, const Vec4& DefaultValue) const;
        UVec4 ReadUVec4(const cgltf_accessor* Accessor, cgltf_size Index, const UVec4& DefaultValue) const;
        std::size_t ResolveMaterialGroupItemIndex(const cgltf_primitive& Primitive) const;
        PrimitiveVertexData BuildPrimitiveVertexData(const cgltf_primitive& Primitive) const;
        void AppendPrimitive(const cgltf_primitive& Primitive, VertexAttributes& OutVertices, std::vector<std::uint32_t>& OutIndices, std::vector<ModelNode::SubMesh>& OutSubMeshes) const;
        void AppendMesh(const cgltf_mesh& Mesh, VertexAttributes& OutVertices, std::vector<std::uint32_t>& OutIndices, std::vector<ModelNode::SubMesh>& OutSubMeshes) const;

    private:
        ModelResult& mResult;
        std::vector<ModelNode*> mNodeStack{};
        const std::unordered_map<const cgltf_material*, std::size_t>* mMaterialLookup{ nullptr };
        GraphicsAPI mApi{ GraphicsAPI::DirectX };
        bool mIsUvFlipEnabled{ false };
    };
}
