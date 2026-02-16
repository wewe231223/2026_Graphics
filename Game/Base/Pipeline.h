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
			ID3D12PipelineState* Get() const;
			static bool HasCompiledPipeline(const std::string& pipelineName);

		private:
			ComPtr<ID3D12PipelineState> mPipelineState{};
		};

		bool PreCompilePipelines(ID3D12Device* device);

	}
}
