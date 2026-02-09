#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Common.h"
#include "SceneVisitor.h"

namespace asset {
    class MaterialVisitor final : public ISceneNodeVisitor {
    public:
        MaterialVisitor();
        ~MaterialVisitor();

        MaterialVisitor(const MaterialVisitor& Other) = delete;
        MaterialVisitor& operator=(const MaterialVisitor& Other) = delete;
        MaterialVisitor(MaterialVisitor&& Other) = delete;
        MaterialVisitor& operator=(MaterialVisitor&& Other) = delete;

    public:
        void OnNodeBegin(const ufbx_scene& Scene, const ufbx_node& Node, const NodeVisitContext& Context) override;
        void OnNodeEnd(const ufbx_scene& Scene, const ufbx_node& Node) override;

        std::vector<Material>& GetMaterials();
        const std::unordered_map<const ufbx_material*, std::size_t>& GetMaterialLookup() const;
        void Clear();

    private:
        std::vector<Material> mMaterials{};
        std::unordered_map<const ufbx_material*, std::size_t> mMaterialLookup{};
        std::unordered_set<const ufbx_material*> mVisitedMaterials{};
    };
}
