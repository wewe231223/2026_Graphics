#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include "Game/Base/Common.h"
#include "Asset/AssetBundle.h"
#include "Utility/DirectXInclude.h"
#include "Core/DX/AllocationHandle.h"
#include "Core/DX/CopyQueue.h"
#include "Core/DX/GraphicsAllocator.h"

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
        const SimpleMath::Matrix& GetGeometryToNode() const;
        const std::vector<std::uint32_t>& GetChildren() const;
        const std::vector<ModelSubMesh>& GetSubMeshes() const;
        const ModelSubMesh& GetSubMesh(std::size_t index) const; 

        bool HasVertexData() const;
        const std::vector<D3D12_VERTEX_BUFFER_VIEW>& GetVertexBufferViews() const;
        const D3D12_INDEX_BUFFER_VIEW& GetIndexBufferView() const;

        std::size_t GetVertexAttributeBufferCount() const;
        VertexAttributeKind GetVertexAttributeKind(std::size_t AttributeIndex) const;
        std::span<const std::byte> GetVertexAttributeRawData(std::size_t AttributeIndex) const;

    private:
        struct VertexAttributeRange final {
            VertexAttributeKind Kind{};
            std::size_t Offset{ 0 };
            std::size_t Size{ 0 };
            UINT StrideInBytes{ 0 };
        };

    private:
        friend class Model;
        void SetBasicData(std::uint32_t IdValue, std::string NameValue, const SimpleMath::Matrix& NodeToParentValue, const SimpleMath::Matrix& GeometryToNodeValue, std::vector<std::uint32_t> ChildrenValue, std::vector<ModelSubMesh> SubMeshesValue);
        void SetVertexData(std::vector<std::byte> VertexRawDataValue, std::vector<VertexAttributeRange> VertexAttributeRangesValue, Core::DX::AllocationHandle VertexAllocationValue, std::vector<D3D12_VERTEX_BUFFER_VIEW> VertexBufferViewsValue);
        void SetIndexData(std::vector<std::byte> IndexRawDataValue, Core::DX::AllocationHandle IndexAllocationValue, const D3D12_INDEX_BUFFER_VIEW& IndexBufferViewValue);

    private:
        std::uint32_t mId{ 0 };
        std::string mName{};
        SimpleMath::Matrix mNodeToParent{};
        SimpleMath::Matrix mGeometryToNode{};
        std::vector<std::uint32_t> mChildren{};
        std::vector<ModelSubMesh> mSubMeshes{};

        std::vector<std::byte> mVertexRawData{};
        std::vector<VertexAttributeRange> mVertexAttributeRanges{};
        Core::DX::AllocationHandle mVertexAllocation{};
        std::vector<D3D12_VERTEX_BUFFER_VIEW> mVertexBufferViews{};

        std::vector<std::byte> mIndexRawData{};
        Core::DX::AllocationHandle mIndexAllocation{};
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
        bool InitializeFromAssetBundle(const asset::AssetBundle& Bundle, const std::vector<std::size_t>& MaterialIndexRemap, Core::DX::GraphicsAllocator& Allocator, Core::DX::CopyQueue& CopyQueue);
        const ModelNode* GetRootNode() const;
        const ModelNode* FindNodeByName(const std::string& NodeName) const;
        const std::vector<ModelNode>& GetNodes() const;

    private:
        bool UploadVertexData(const asset::VertexAttributes& Vertices, Core::DX::GraphicsAllocator& Allocator, Core::DX::CopyQueue& CopyQueue, std::vector<std::byte>& OutRawData, std::vector<ModelNode::VertexAttributeRange>& OutRanges, Core::DX::AllocationHandle& OutAllocation, std::vector<D3D12_VERTEX_BUFFER_VIEW>& OutViews) const;
        bool UploadIndexData(const std::vector<std::uint32_t>& Indices, Core::DX::GraphicsAllocator& Allocator, Core::DX::CopyQueue& CopyQueue, std::vector<std::byte>& OutRawData, Core::DX::AllocationHandle& OutAllocation, D3D12_INDEX_BUFFER_VIEW& OutView) const;

    private:
        std::vector<ModelNode> mNodes{};
        std::unordered_map<std::string, std::uint32_t> mNodeNameLookup{};
        std::uint32_t mRootNodeIndex{ 0 };
        bool mHasRootNode{ false };
    };
}
