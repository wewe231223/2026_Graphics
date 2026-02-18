#pragma once
#include <string>
#include "RootSignature.h"

namespace Game {
	namespace Base {
		class Pipeline {
		public:
			Pipeline() = default;
			~Pipeline() = default;
			Pipeline(const Pipeline& other) = default;
			Pipeline& operator=(const Pipeline& other) = default;
			Pipeline(Pipeline&& other) noexcept = default;
			Pipeline& operator=(Pipeline&& other) noexcept = default;

		public:
			bool Initialize(const std::string& pipelineName);
			Pipeline* Set(Pipeline* pipeline, ID3D12GraphicsCommandList* commandList);
			ID3D12PipelineState* Get() const;
			ID3D12RootSignature* GetRootSignature() const;

			bool operator==(const Pipeline& other) const;
			bool operator!=(const Pipeline& other) const;
			bool operator<(const Pipeline& other) const;

			static bool HasCompiledPipeline(const std::string& pipelineName);

		private:
			ComPtr<ID3D12RootSignature> mRootSignature{};
			ComPtr<ID3D12PipelineState> mPipelineState{};
		};

		bool PreCompilePipelines(ID3D12Device* device);

	}
}
