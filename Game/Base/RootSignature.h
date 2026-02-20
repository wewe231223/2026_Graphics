#pragma once
#include <string>
#include "Utility/DirectXInclude.h"

namespace Game {
	namespace Base {
		class RootSignature {
		public:
			RootSignature() = default;
			~RootSignature() = default;
			RootSignature(const RootSignature& other) = default;
			RootSignature& operator=(const RootSignature& other) = default;
			RootSignature(RootSignature&& other) noexcept = default;
			RootSignature& operator=(RootSignature&& other) noexcept = default;

		public:
			bool Initialize(const std::string& rootSignatureName);
			ID3D12RootSignature* Get() const;
			static bool HasCompiledRootSignature(const std::string& rootSignatureName);

		private:
			ComPtr<ID3D12RootSignature> mRootSignature{};
		};

		bool PreCompileRootSignatures(ID3D12Device* device);

	}
}
