#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "Asset/ModelResult.h"
#include "Core/Common.h"
#include "Game/Base/Common.h"
#include "Utility/DirectXInclude.h"

namespace Game {
    class ModelNode final : public Interface::IModelNode {
    public:
        ModelNode();
        ~ModelNode();
        ModelNode(const ModelNode& Other) = delete;
        ModelNode& operator=(const ModelNode& Other) = delete;
        ModelNode(ModelNode&& Other) noexcept;
        ModelNode& operator=(ModelNode&& Other) noexcept;

    public:
        std::uint32_t GetId() const;
        const std::string& GetName() const;
        const SimpleMath::Matrix& GetNodeToParent() const;
        const std::vector<std::uint32_t>& GetChildren() const;
        const std::vector<ModelSubMesh>& GetSubMeshes() const;
        const ModelSubMesh& GetSubMesh(std::size_t Index) const;
        const std::vector<ModelBoneInfo>& GetBoneInfos() const;
        bool HasBoneInfo() const;
        bool IsSkinnedMesh() const;

        bool HasVertexData() const;
        const std::vector<D3D12_VERTEX_BUFFER_VIEW>& GetVertexBufferViews() const;
        const D3D12_INDEX_BUFFER_VIEW& GetIndexBufferView() const;

        std::size_t GetVertexAttributeBufferCount() const;
        VertexAttributeKind GetVertexAttributeKind(std::size_t AttributeIndex) const;
        std::span<const std::byte> GetVertexAttributeRawData(std::size_t AttributeIndex) const;

    private:
        struct VertexAttributeRange final {
        public:
            VertexAttributeKind Kind{};
            std::size_t Offset{ 0 };
            std::size_t Size{ 0 };
            UINT StrideInBytes{ 0 };
        };

    private:
        friend class Model;
        void SetBasicData(std::uint32_t IdValue, std::string NameValue, const SimpleMath::Matrix& NodeToParentValue, std::vector<std::uint32_t> ChildrenValue, std::vector<ModelSubMesh> SubMeshesValue, std::vector<ModelBoneInfo> BoneInfosValue, bool IsSkinnedMeshValue);
        void SetVertexData(std::vector<std::byte> VertexRawDataValue, std::vector<VertexAttributeRange> VertexAttributeRangesValue, std::unique_ptr<Interface::IAllocationHandle> VertexAllocationValue, std::vector<D3D12_VERTEX_BUFFER_VIEW> VertexBufferViewsValue);
        void SetIndexData(std::vector<std::byte> IndexRawDataValue, std::unique_ptr<Interface::IAllocationHandle> IndexAllocationValue, const D3D12_INDEX_BUFFER_VIEW& IndexBufferViewValue);

    private:
        std::uint32_t mId{ 0 };
        std::string mName{};
        SimpleMath::Matrix mNodeToParent{};
        std::vector<std::uint32_t> mChildren{};
        std::vector<ModelSubMesh> mSubMeshes{};
        std::vector<ModelBoneInfo> mBoneInfos{};
        bool mIsSkinnedMesh{ false };
        std::vector<std::byte> mVertexRawData{};
        std::vector<VertexAttributeRange> mVertexAttributeRanges{};
        std::unique_ptr<Interface::IAllocationHandle> mVertexAllocation{};
        std::vector<D3D12_VERTEX_BUFFER_VIEW> mVertexBufferViews{};
        std::vector<std::byte> mIndexRawData{};
        std::unique_ptr<Interface::IAllocationHandle> mIndexAllocation{};
        D3D12_INDEX_BUFFER_VIEW mIndexBufferView{};
        bool mHasVertexData{ false };
    };

    class Model final {
    public:
        Model();
        ~Model();
        Model(const Model& Other) = delete;
        Model& operator=(const Model& Other) = delete;
        Model(Model&& Other) noexcept;
        Model& operator=(Model&& Other) noexcept;

    public:
        bool InitializeFromModelResult(const asset::ModelResult& ModelData, Interface::IGraphicsAllocator* Allocator, Interface::ICopyQueue* CopyQueue);
        const ModelNode* GetRootNode() const;
        const ModelNode* FindNodeByName(const std::string& NodeName) const;
        const std::vector<ModelNode>& GetNodes() const;

    private:
        bool UploadVertexData(const asset::VertexAttributes& Vertices, Interface::IGraphicsAllocator* Allocator, Interface::ICopyQueue* CopyQueue, std::vector<std::byte>& OutRawData, std::vector<ModelNode::VertexAttributeRange>& OutRanges, std::unique_ptr<Interface::IAllocationHandle>& OutAllocation, std::vector<D3D12_VERTEX_BUFFER_VIEW>& OutViews) const;
        bool UploadIndexData(const std::vector<std::uint32_t>& Indices, Interface::IGraphicsAllocator* Allocator, Interface::ICopyQueue* CopyQueue, std::vector<std::byte>& OutRawData, std::unique_ptr<Interface::IAllocationHandle>& OutAllocation, D3D12_INDEX_BUFFER_VIEW& OutView) const;

    private:
        std::vector<ModelNode> mNodes{};
        std::unordered_map<std::string, std::uint32_t> mNodeNameLookup{};
        std::uint32_t mRootNodeIndex{ 0 };
        bool mHasRootNode{ false };
    };
}
