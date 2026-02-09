#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Common.h"

namespace asset {
    class ModelNode final {
    public:
        using Id = std::uint32_t;
        struct SubMesh final {
        public:
            std::size_t IndexOffset{ 0 };
            std::size_t IndexCount{ 0 };
            std::size_t MaterialIndex{ 0 };
        };

    public:
        ModelNode(Id IdValue, std::string Name);
        ~ModelNode() = default;

        ModelNode(const ModelNode& Other) = delete;
        ModelNode& operator=(const ModelNode& Other) = delete;
        ModelNode(ModelNode&& Other) = default;
        ModelNode& operator=(ModelNode&& Other) = default;

    public:
        Id GetId() const;
        const std::string& GetName() const;

        const Mat4& GetNodeToParent() const;
        void SetNodeToParent(const Mat4& NodeToParent);

        const Mat4& GetGeometryToNode() const;
        void SetGeometryToNode(const Mat4& GeometryToNode);

        ModelNode* GetParent() const;
        const std::vector<ModelNode*>& GetChildren() const;
        void AddChild(ModelNode* Child);

        VertexAttributes& Vertices();
        const VertexAttributes& Vertices() const;

        std::vector<std::uint32_t>& Indices();
        const std::vector<std::uint32_t>& Indices() const;

        std::vector<SubMesh>& SubMeshes();
        const std::vector<SubMesh>& SubMeshes() const;

        std::vector<const ModelNode*> GetChildChain() const;
        void SetSubMeshes(std::vector<SubMesh> SubMeshes);
        const std::vector<SubMesh>& GetSubMeshes() const;

    private:
        Id mId{ 0 };

        std::string mName{};
        Mat4 mNodeToParent{ 1.0f };
        Mat4 mGeometryToNode{ 1.0f };

        ModelNode* mParent{ nullptr };
        std::vector<ModelNode*> mChildren{};

        VertexAttributes mVertices{};
        std::vector<std::uint32_t> mIndices{};
        std::vector<SubMesh> mSubMeshes{};
    };

    class ModelResult final {
    public:
        ModelResult();
        ~ModelResult() = default;

        ModelResult(const ModelResult& Other) = delete;
        ModelResult& operator=(const ModelResult& Other) = delete;
        ModelResult(ModelResult&& Other) = default;
        ModelResult& operator=(ModelResult&& Other) = default;

    public:
        ModelNode* GetRoot() const;
        std::size_t NodeCount() const;
        const std::vector<std::unique_ptr<ModelNode>>& Nodes() const;

        ModelNode& CreateNode(std::string Name, ModelNode* Parent);
        void ForEachDfs(const std::function<void(ModelNode&)>& Function) const;

    private:
        std::vector<std::unique_ptr<ModelNode>> mNodes{};
        ModelNode* mRoot{ nullptr };
        std::uint64_t mNextId{ 1 };
    };
}
