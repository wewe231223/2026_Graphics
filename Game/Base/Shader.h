#pragma once 
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include "Utility/DirectXInclude.h"

namespace fs = std::filesystem;


namespace Game {
	namespace Base {
		class Shader {
		public:
			struct ShaderByteCode {
				std::string Identifier{};
				std::vector<uint8_t> ByteCode{};
			};

		public:
			Shader() = default;
			~Shader() = default;
			Shader(const Shader& other);
			Shader& operator=(const Shader& other);
			Shader(Shader&& other) noexcept;
			Shader& operator=(Shader&& other) noexcept;

		public:
			void Initialize(const fs::path& sourceFileName); 

			const std::wstring& GetSourceFileName() const;
			bool HasByteCode(const std::string& identifier) const;
			D3D12_SHADER_BYTECODE GetByteCode(const std::string& identifier) const;
			const std::vector<ShaderByteCode>& GetAllByteCodes() const;

		private:
			void InitializeFromCompiledStorage();

		private:
			std::wstring mSourceFileName{};
			std::vector<ShaderByteCode> mShaderByteCodes{};
			std::unordered_map<std::string, std::size_t> mIdentifierToIndex{};
		};

		bool PreCompileShaders();


	}
}
