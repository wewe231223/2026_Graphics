#pragma once
#include <span>
#include <string>
#include <vector>
#include "Game/Base/Common.h"
#include "RootSignature.h"

namespace Game {
	namespace Base {
		class Pipeline : public RenderContract::IPipeline {
		public:
			Pipeline() = default;
			~Pipeline() = default;
			Pipeline(const Pipeline& other) = default;
			Pipeline& operator=(const Pipeline& other) = default;
			Pipeline(Pipeline&& other) noexcept = default;
			Pipeline& operator=(Pipeline&& other) noexcept = default;

		public:
			bool Initialize(const std::string& pipelineName);
			const RenderContract::IPipeline* Set(const RenderContract::IPipeline* pipeline, ID3D12GraphicsCommandList* commandList) const;
			ID3D12PipelineState* Get() const;
			ID3D12RootSignature* GetRootSignature() const;
			D3D_PRIMITIVE_TOPOLOGY GetPrimitiveTopology() const;
			std::span<const VertexInputBinding> GetVertexInputBindings() const;
			bool HasOption(PipelineOption Option) const;

			bool operator==(const Pipeline& other) const;
			bool operator!=(const Pipeline& other) const;
			bool operator<(const Pipeline& other) const;

			static bool HasCompiledPipeline(const std::string& pipelineName);

		private:
			ComPtr<ID3D12RootSignature> mRootSignature{};
			ComPtr<ID3D12PipelineState> mPipelineState{};
			D3D_PRIMITIVE_TOPOLOGY mPrimitiveTopology{ D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST };
			std::vector<VertexInputBinding> mVertexInputBindings{};
			std::uint32_t mOptionMask{};
		};

		bool PreCompilePipelines(ID3D12Device* device);
	}
}
