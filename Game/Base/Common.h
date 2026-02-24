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
    namespace Interface {
        class IPipeline abstract {
        public:
            virtual ~IPipeline() = default;

            virtual bool Initialize(const std::string& pipelineName)                                PURE;
            virtual IPipeline* Set(IPipeline* pipeline, ID3D12GraphicsCommandList* commandList)     PURE;
            virtual ID3D12PipelineState* Get() const                                                PURE;
            virtual ID3D12RootSignature* GetRootSignature() const                                   PURE;
        };
    }
}

namespace Game {
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

    struct ModelSubMesh final {
        std::size_t IndexOffset{ 0 };
        std::size_t IndexCount{ 0 };
        std::size_t MaterialGroupItemIndex{ 0 };
    };
}

namespace Game {
    namespace Interface {
        class IModelNode abstract {
        public:
            virtual ~IModelNode() = default;

            virtual std::uint32_t GetId() const                                                             PURE;
            virtual const std::string& GetName() const                                                      PURE;
            virtual const SimpleMath::Matrix& GetNodeToParent() const                                       PURE;
            virtual const SimpleMath::Matrix& GetGeometryToNode() const                                     PURE;
            virtual const std::vector<std::uint32_t>& GetChildren() const                                   PURE;
            virtual const std::vector<ModelSubMesh>& GetSubMeshes() const                                   PURE;
            virtual const ModelSubMesh& GetSubMesh(std::size_t index) const                                 PURE;

            virtual bool HasVertexData() const                                                              PURE;
            virtual const std::vector<D3D12_VERTEX_BUFFER_VIEW>& GetVertexBufferViews() const               PURE;
            virtual const D3D12_INDEX_BUFFER_VIEW& GetIndexBufferView() const                               PURE;

            virtual std::size_t GetVertexAttributeBufferCount() const                                       PURE;
            virtual VertexAttributeKind GetVertexAttributeKind(std::size_t AttributeIndex) const            PURE;
            virtual std::span<const std::byte> GetVertexAttributeRawData(std::size_t AttributeIndex) const  PURE;
        };
    }
}