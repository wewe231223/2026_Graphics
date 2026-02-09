#pragma once

#include "Common.h"

namespace asset {
    struct NodeVisitContext final {
    public:
        const ufbx_node* mParent{ nullptr };
        Mat4 mNodeToParent{ 1.0f };
        Mat4 mGeometryToNode{ 1.0f };
    };

    class ISceneNodeVisitor {
    public:
        ISceneNodeVisitor() = default;
        virtual ~ISceneNodeVisitor() = default;

        ISceneNodeVisitor(const ISceneNodeVisitor& Other) = default;
        ISceneNodeVisitor& operator=(const ISceneNodeVisitor& Other) = default;
        ISceneNodeVisitor(ISceneNodeVisitor&& Other) noexcept = default;
        ISceneNodeVisitor& operator=(ISceneNodeVisitor&& Other) noexcept = default;

    public:
        virtual void OnNodeBegin(const ufbx_scene& Scene, const ufbx_node& Node, const NodeVisitContext& Ctx) = 0;
        virtual void OnNodeEnd(const ufbx_scene& Scene, const ufbx_node& Node) = 0;
    };
}
