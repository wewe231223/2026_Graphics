#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Common.h"
#include "GltfSceneVisitor.h"

namespace asset {
    class GltfMaterialVisitor final : public IGltfSceneNodeVisitor {
    public:
        GltfMaterialVisitor();
        ~GltfMaterialVisitor();

        GltfMaterialVisitor(const GltfMaterialVisitor& Other) = delete;
        GltfMaterialVisitor& operator=(const GltfMaterialVisitor& Other) = delete;
        GltfMaterialVisitor(GltfMaterialVisitor&& Other) = delete;
        GltfMaterialVisitor& operator=(GltfMaterialVisitor&& Other) = delete;

    public:
        void OnNodeBegin(const cgltf_data& Scene, const cgltf_node& Node, const GltfNodeVisitContext& Context) override;
        void OnNodeEnd(const cgltf_data& Scene, const cgltf_node& Node) override;

        std::vector<Material>& GetMaterials();
        const std::unordered_map<const cgltf_material*, std::size_t>& GetMaterialLookup() const;
        void Clear();

    private:
        std::vector<Material> mMaterials{};
        std::unordered_map<const cgltf_material*, std::size_t> mMaterialLookup{};
        std::unordered_set<const cgltf_material*> mVisitedMaterials{};
    };
}
