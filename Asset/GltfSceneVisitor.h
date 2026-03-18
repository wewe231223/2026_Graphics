#pragma once

#include "Common.h"
#include "cgltf.h"

namespace asset {
    struct GltfNodeVisitContext final {
    public:
        const cgltf_node* mParent{ nullptr };
        Mat4 mNodeToParent{ 1.0f };
        Mat4 mGeometryToNode{ 1.0f };
    };

    class IGltfSceneNodeVisitor {
    public:
        IGltfSceneNodeVisitor() = default;
        virtual ~IGltfSceneNodeVisitor() = default;

        IGltfSceneNodeVisitor(const IGltfSceneNodeVisitor& Other) = default;
        IGltfSceneNodeVisitor& operator=(const IGltfSceneNodeVisitor& Other) = default;
        IGltfSceneNodeVisitor(IGltfSceneNodeVisitor&& Other) noexcept = default;
        IGltfSceneNodeVisitor& operator=(IGltfSceneNodeVisitor&& Other) noexcept = default;

    public:
        virtual void OnNodeBegin(const cgltf_data& Scene, const cgltf_node& Node, const GltfNodeVisitContext& Context) = 0;
        virtual void OnNodeEnd(const cgltf_data& Scene, const cgltf_node& Node) = 0;
    };
}
