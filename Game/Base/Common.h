#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <span>
#include <cstddef>
#include <d3d12.h>
#include "DirectXTK12/SimpleMath.h"

#ifndef PURE 
#define PURE = 0
#endif 


namespace SimpleMath = DirectX::SimpleMath;

namespace Game {
    enum class PipelineOption : std::uint32_t {
        None = 0,
        DepthAlphaCutoff = 1u << 0
    };

    struct VertexInputBinding;
}

namespace Interface {
    class IPipeline abstract {
    public:
        virtual ~IPipeline() = default;

        virtual bool Initialize(const std::string& pipelineName)                                                    PURE;
        virtual const IPipeline* Set(const IPipeline* pipeline, ID3D12GraphicsCommandList* commandList) const       PURE;
        virtual ID3D12PipelineState* Get() const                                                                    PURE;
        virtual ID3D12RootSignature* GetRootSignature() const                                                       PURE;
        virtual D3D_PRIMITIVE_TOPOLOGY GetPrimitiveTopology() const                                                 PURE;
        virtual std::span<const Game::VertexInputBinding> GetVertexInputBindings() const                           PURE;
        virtual bool HasOption(Game::PipelineOption Option) const                                                   PURE;
    };
}


namespace Game {
    struct ModelBoneInfo final {
        std::uint32_t SkinArrayIndex{ 0 };
        std::uint32_t JointArrayIndex{ 0 };
        std::string BoneName{};
        SimpleMath::Matrix InverseBindMatrix{};
    };

    struct RuntimeBoneInfo final {
        std::uint32_t SkinArrayIndex{ 0 };
        std::uint32_t JointArrayIndex{ 0 };
        SimpleMath::Matrix InverseBindMatrix{};
    };

    enum class VertexAttributeKind : std::uint32_t {
        Position,
        Normal,
        TexCoord0,
        TexCoord1,
        TexCoord2,
        TexCoord3,
        Color,
        Tangent,
        Bitangent,
        BoneIndices,
        BoneWeights
    };

    struct VertexInputBinding final {
        VertexAttributeKind Kind{};
        std::uint32_t InputSlot{ 0 };
    };

    struct ModelSubMesh final {
        std::size_t IndexOffset{ 0 };
        std::size_t IndexCount{ 0 };
        std::size_t MaterialGroupItemIndex{ 0 };
    };
}


namespace Interface {
    class IModelNode abstract {
    public:
        virtual ~IModelNode() = default;

        virtual std::uint32_t GetId() const                                                             PURE;
        virtual const std::string& GetName() const                                                      PURE;
        virtual const SimpleMath::Matrix& GetNodeToParent() const                                       PURE;
        virtual const std::vector<std::uint32_t>& GetChildren() const                                   PURE;
        virtual const std::vector<Game::ModelSubMesh>& GetSubMeshes() const                             PURE;
        virtual const Game::ModelSubMesh& GetSubMesh(std::size_t index) const                           PURE;
        virtual const std::vector<Game::ModelBoneInfo>& GetBoneInfos() const                            PURE;
        virtual bool HasBoneInfo() const                                                                 PURE;

        virtual bool HasVertexData() const                                                              PURE;
        virtual const std::vector<D3D12_VERTEX_BUFFER_VIEW>& GetVertexBufferViews() const               PURE;
        virtual const D3D12_INDEX_BUFFER_VIEW& GetIndexBufferView() const                               PURE;

        virtual std::size_t GetVertexAttributeBufferCount() const                                       PURE;
        virtual Game::VertexAttributeKind GetVertexAttributeKind(std::size_t AttributeIndex) const            PURE;
        virtual bool TryGetVertexBufferView(Game::VertexAttributeKind Kind, D3D12_VERTEX_BUFFER_VIEW& OutView) const PURE;
        virtual std::span<const std::byte> GetVertexAttributeRawData(std::size_t AttributeIndex) const  PURE;
    };
}
