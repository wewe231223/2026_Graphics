#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <d3d12.h>

#include "RenderContract/Model/ModelBoneInfo.h"
#include "RenderContract/Model/ModelSubMesh.h"
#include "RenderContract/Model/VertexInputBinding.h"
#include "RenderContract/Pipeline/PipelineOption.h"

namespace RenderContract {
    class IFutureSyncObject {
    public:
        virtual ~IFutureSyncObject() = default;

    public:
        virtual bool IsFutureComplete(std::uint64_t FutureTicket) const = 0;
        virtual void WaitFuture(std::uint64_t FutureTicket) const = 0;
        virtual void QueueWaitFuture(ID3D12CommandQueue* WaitingQueue, std::uint64_t FutureTicket) const = 0;
        virtual void QueueWaitFutures(ID3D12CommandQueue* WaitingQueue, std::span<const std::uint64_t> FutureTickets) const;
    };

    class IPipeline abstract {
    public:
        virtual ~IPipeline() = default;

    public:
        virtual bool Initialize(const std::string& PipelineName) = 0;
        virtual const IPipeline* Set(const IPipeline* Pipeline, ID3D12GraphicsCommandList* CommandList) const = 0;
        virtual ID3D12PipelineState* Get() const = 0;
        virtual ID3D12RootSignature* GetRootSignature() const = 0;
        virtual D3D_PRIMITIVE_TOPOLOGY GetPrimitiveTopology() const = 0;
        virtual std::span<const VertexInputBinding> GetVertexInputBindings() const = 0;
        virtual bool HasOption(PipelineOption Option) const = 0;
    };

    class IModelNode abstract {
    public:
        virtual ~IModelNode() = default;

    public:
        virtual std::uint32_t GetId() const = 0;
        virtual const std::string& GetName() const = 0;
        virtual const DirectX::SimpleMath::Matrix& GetNodeToParent() const = 0;
        virtual const std::vector<std::uint32_t>& GetChildren() const = 0;
        virtual const std::vector<ModelSubMesh>& GetSubMeshes() const = 0;
        virtual const ModelSubMesh& GetSubMesh(std::size_t Index) const = 0;
        virtual const std::vector<ModelBoneInfo>& GetBoneInfos() const = 0;
        virtual bool HasBoneInfo() const = 0;
        virtual bool HasVertexData() const = 0;
        virtual const std::vector<D3D12_VERTEX_BUFFER_VIEW>& GetVertexBufferViews() const = 0;
        virtual const D3D12_INDEX_BUFFER_VIEW& GetIndexBufferView() const = 0;
        virtual std::size_t GetVertexAttributeBufferCount() const = 0;
        virtual VertexAttributeKind GetVertexAttributeKind(std::size_t AttributeIndex) const = 0;
        virtual bool TryGetVertexBufferView(VertexAttributeKind Kind, D3D12_VERTEX_BUFFER_VIEW& OutView) const = 0;
        virtual std::span<const std::byte> GetVertexAttributeRawData(std::size_t AttributeIndex) const = 0;
    };
}
