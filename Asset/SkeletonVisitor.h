#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "ModelResult.h"
#include "SceneVisitor.h"

namespace asset {
    class SkeletonVisitor final : public ISceneNodeVisitor {
    public:
        explicit SkeletonVisitor(ModelResult& OutResult);
        ~SkeletonVisitor() = default;

        SkeletonVisitor(const SkeletonVisitor& Other) = delete;
        SkeletonVisitor& operator=(const SkeletonVisitor& Other) = delete;
        SkeletonVisitor(SkeletonVisitor&& Other) = delete;
        SkeletonVisitor& operator=(SkeletonVisitor&& Other) = delete;

    public:
        void OnNodeBegin(const ufbx_scene& Scene, const ufbx_node& Node, const NodeVisitContext& Context) override;
        void OnNodeEnd(const ufbx_scene& Scene, const ufbx_node& Node) override;

        void Finalize(const ufbx_scene& Scene);

    private:
        static Mat4 ToMat4(const ufbx_matrix& Matrix);
        static std::string ToString(const ufbx_string& StringValue);

        void RegisterBone(const ufbx_node& Node);
        void RegisterSkin(const ufbx_node& Node, const ufbx_skin_deformer& Skin);

    private:
        ModelResult& mResult;
        SkeletonData mSkeletonData{};
        std::unordered_map<std::uint32_t, std::size_t> mBoneIndexByNodeTypedId{};
        std::unordered_map<std::uint32_t, std::size_t> mClusterIndexByClusterTypedId{};
    };
}
