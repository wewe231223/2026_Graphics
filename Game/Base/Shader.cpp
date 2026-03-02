#include "Shader.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include "Utility/StringUtils.h"

namespace {
	using namespace Game::Base;

	struct ShaderCompileOption {
		std::wstring SourceFileName{};
		std::wstring EntryPoint{};
		std::wstring Profile{};
		std::vector<std::wstring> Arguments{};
	};

	struct CompiledShaderCollection {
		std::unordered_map<std::string, std::vector<uint8_t>> ByteCodesByIdentifier{};
	};

	std::filesystem::path GetShaderRootPath() {
		return std::filesystem::current_path() / "Shader";
	}

	std::filesystem::path GetShaderSourcePath(const std::wstring& sourceFileName) {
		return GetShaderRootPath() / "Source" / std::filesystem::path(sourceFileName);
	}
	  
	std::filesystem::path GetShaderBinaryPath(const std::wstring& sourceFileName) {
		std::filesystem::path sourcePath{ sourceFileName };
		std::wstring binaryFileName{ sourcePath.stem().wstring() + L".shaderbin" };
		return GetShaderRootPath() / "Binarys" / binaryFileName;
	}

	std::string BuildIdentifier(const ShaderCompileOption& option) {
		std::string profile{ ConvertWstringToUtf8(option.Profile) };
		std::string entryPoint{ ConvertWstringToUtf8(option.EntryPoint) };
		return profile + ":" + entryPoint;
	}

	std::wstring Trim(const std::wstring& value) {
		std::size_t firstIndex{ value.find_first_not_of(L" \t\r\n") };
		if (firstIndex == std::wstring::npos) {
			return {};
		}

		std::size_t lastIndex{ value.find_last_not_of(L" \t\r\n") };
		return value.substr(firstIndex, lastIndex - firstIndex + 1);
	}

	std::optional<ShaderCompileOption> ParseMetadataLine(const std::wstring& line) {
		std::wstringstream stream{ line };
		std::wstring token{};
		ShaderCompileOption option{};
		while (std::getline(stream, token, L'|')) {
			std::size_t equalIndex{ token.find(L'=') };
			if (equalIndex == std::wstring::npos) {
				continue;
			}

			std::wstring key{ Trim(token.substr(0, equalIndex)) };
			std::wstring value{ Trim(token.substr(equalIndex + 1)) };
			if (key == L"Source") {
				option.SourceFileName = value;
			}
			else if (key == L"Entry") {
				option.EntryPoint = value;
			}
			else if (key == L"Profile") {
				option.Profile = value;
			}
			else if (key == L"Arguments") {
				std::wstringstream argumentStream{ value };
				std::wstring argument{};
				while (std::getline(argumentStream, argument, L';')) {
					std::wstring trimmedArgument{ Trim(argument) };
					if (!trimmedArgument.empty()) {
						option.Arguments.emplace_back(trimmedArgument);
					}
				}
			}
		}

		if (option.SourceFileName.empty() || option.EntryPoint.empty() || option.Profile.empty()) {
			return std::nullopt;
		}

		return option;
	}

	std::unordered_map<std::wstring, std::vector<ShaderCompileOption>> ParseMetadataFile() {
		std::unordered_map<std::wstring, std::vector<ShaderCompileOption>> optionsBySource{};
		std::filesystem::path metadataPath{ GetShaderRootPath() / "ShaderMetadata.txt" };
		if (!std::filesystem::exists(metadataPath)) {
			return optionsBySource;
		}

		std::wifstream file{ metadataPath };
		if (!file.is_open()) {
			return optionsBySource;
		}

		std::wstring line{};
		while (std::getline(file, line)) {
			std::wstring trimmedLine{ Trim(line) };
			if (trimmedLine.empty()) {
				continue;
			}

			if (trimmedLine[0] == L'#') {
				continue;
			}

			std::optional<ShaderCompileOption> option{ ParseMetadataLine(trimmedLine) };
			if (!option.has_value()) {
				continue;
			}

			optionsBySource[option->SourceFileName].emplace_back(std::move(option.value()));
		}

		return optionsBySource;
	}

	std::set<std::filesystem::path> CollectDependenciesRecursive(const std::filesystem::path& filePath, const std::filesystem::path& shaderSourceRoot, std::set<std::filesystem::path>& visited) {
		std::set<std::filesystem::path> dependencies{};
		std::filesystem::path normalizedPath{ std::filesystem::weakly_canonical(filePath) };
		if (visited.contains(normalizedPath)) {
			return dependencies;
		}

		visited.insert(normalizedPath);
		dependencies.insert(normalizedPath);
		std::ifstream input{ normalizedPath };
		if (!input.is_open()) {
			return dependencies;
		}

		std::string line{};
		std::regex includeRegex{ R"(^\s*#\s*include\s*\"([^\"]+)\")" };
		while (std::getline(input, line)) {
			std::smatch match{};
			if (!std::regex_search(line, match, includeRegex)) {
				continue;
			}

			std::filesystem::path includePath{ normalizedPath.parent_path() / match[1].str() };
			if (!std::filesystem::exists(includePath)) {
				includePath = shaderSourceRoot / match[1].str();
			}

			if (!std::filesystem::exists(includePath)) {
				continue;
			}

			std::set<std::filesystem::path> childDependencies{ CollectDependenciesRecursive(includePath, shaderSourceRoot, visited) };
			dependencies.insert(childDependencies.begin(), childDependencies.end());
		}

		return dependencies;
	}

	std::set<std::filesystem::path> CollectDependencies(const std::filesystem::path& sourcePath) {
		std::set<std::filesystem::path> visited{};
		return CollectDependenciesRecursive(sourcePath, GetShaderRootPath() / "Source", visited);
	}

	bool NeedsRecompile(const std::wstring& sourceFileName) {
		std::filesystem::path sourcePath{ GetShaderSourcePath(sourceFileName) };
		std::filesystem::path binaryPath{ GetShaderBinaryPath(sourceFileName) };
		if (!std::filesystem::exists(sourcePath) || !std::filesystem::exists(binaryPath)) {
			return true;
		}

		std::filesystem::file_time_type binaryTime{ std::filesystem::last_write_time(binaryPath) };
		std::set<std::filesystem::path> dependencies{ CollectDependencies(sourcePath) };
		for (const std::filesystem::path& dependencyPath : dependencies) {
			if (!std::filesystem::exists(dependencyPath)) {
				return true;
			}

			if (std::filesystem::last_write_time(dependencyPath) > binaryTime) {
				return true;
			}
		}

		return false;
	}

	std::vector<uint8_t> CompileShader(const ShaderCompileOption& option, IDxcUtils* dxcUtils, IDxcCompiler3* dxcCompiler, IDxcIncludeHandler* includeHandler) {
		std::filesystem::path sourcePath{ GetShaderSourcePath(option.SourceFileName) };
		std::ifstream file{ sourcePath, std::ios::binary };
		if (!file.is_open()) {
			return {};
		}

		file.seekg(0, std::ios::end);
		std::streamsize fileSize{ file.tellg() };
		file.seekg(0, std::ios::beg);
		if (fileSize <= 0) {
			return {};
		}

		std::vector<char> sourceData(static_cast<std::size_t>(fileSize));
		file.read(sourceData.data(), fileSize);
		if (!file.good() && !file.eof()) {
			return {};
		}

		DxcBuffer sourceBuffer{};
		sourceBuffer.Ptr = sourceData.data();
		sourceBuffer.Size = sourceData.size();
		sourceBuffer.Encoding = DXC_CP_UTF8;

		std::vector<std::wstring> argumentStorage{};
		argumentStorage.emplace_back(L"-E");
		argumentStorage.emplace_back(option.EntryPoint);
		argumentStorage.emplace_back(L"-T");
		argumentStorage.emplace_back(option.Profile);
		argumentStorage.emplace_back(L"-I");
		argumentStorage.emplace_back((GetShaderRootPath() / "Source").wstring());
		argumentStorage.emplace_back(L"-Zi");
		argumentStorage.emplace_back(L"-Qembed_debug");
		argumentStorage.insert(argumentStorage.end(), option.Arguments.begin(), option.Arguments.end());

		std::vector<LPCWSTR> arguments{};
		arguments.reserve(argumentStorage.size());
		for (const std::wstring& argument : argumentStorage) {
			arguments.emplace_back(argument.c_str());
		}

		ComPtr<IDxcResult> result{ nullptr };
		HRESULT compileResult{ dxcCompiler->Compile(&sourceBuffer, arguments.data(), static_cast<uint32_t>(arguments.size()), includeHandler, IID_PPV_ARGS(result.GetAddressOf())) };
		if (FAILED(compileResult) || result == nullptr) {
			return {};
		}

		ComPtr<IDxcBlobUtf8> errors{ nullptr };
		result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(errors.GetAddressOf()), nullptr);
		if (errors != nullptr && errors->GetStringLength() > 0) {
			return {};
		}

		HRESULT status{ S_OK };
		result->GetStatus(&status);
		if (FAILED(status)) {
			return {};
		}

		ComPtr<IDxcBlob> objectBlob{ nullptr };
		result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(objectBlob.GetAddressOf()), nullptr);
		if (objectBlob == nullptr || objectBlob->GetBufferSize() == 0) {
			return {};
		}

		std::vector<uint8_t> byteCode(objectBlob->GetBufferSize());
		std::memcpy(byteCode.data(), objectBlob->GetBufferPointer(), objectBlob->GetBufferSize());
		return byteCode;
	}

	void WriteString(std::ofstream& output, const std::string& value) {
		uint32_t length{ static_cast<uint32_t>(value.size()) };
		output.write(reinterpret_cast<const char*>(&length), sizeof(uint32_t));
		output.write(value.data(), length);
	}

	std::string ReadString(std::ifstream& input) {
		uint32_t length{};
		input.read(reinterpret_cast<char*>(&length), sizeof(uint32_t));
		std::string value(length, '\0');
		if (length > 0) {
			input.read(value.data(), length);
		}
		return value;
	}

	bool SaveCompiledBinary(const std::wstring& sourceFileName, const CompiledShaderCollection& compiledCollection) {
		std::filesystem::create_directories(GetShaderRootPath() / "Binarys");
		std::ofstream output{ GetShaderBinaryPath(sourceFileName), std::ios::binary | std::ios::trunc };
		if (!output.is_open()) {
			return false;
		}

		uint32_t magic{ 0x30444853 };
		uint32_t version{ 1 };
		uint32_t count{ static_cast<uint32_t>(compiledCollection.ByteCodesByIdentifier.size()) };
		output.write(reinterpret_cast<const char*>(&magic), sizeof(uint32_t));
		output.write(reinterpret_cast<const char*>(&version), sizeof(uint32_t));
		output.write(reinterpret_cast<const char*>(&count), sizeof(uint32_t));
		for (const auto& pair : compiledCollection.ByteCodesByIdentifier) {
			WriteString(output, pair.first);
			uint64_t byteCodeSize{ static_cast<uint64_t>(pair.second.size()) };
			output.write(reinterpret_cast<const char*>(&byteCodeSize), sizeof(uint64_t));
			output.write(reinterpret_cast<const char*>(pair.second.data()), static_cast<std::streamsize>(byteCodeSize));
		}

		return true;
	}

	std::optional<CompiledShaderCollection> LoadCompiledBinary(const std::wstring& sourceFileName) {
		std::ifstream input{ GetShaderBinaryPath(sourceFileName), std::ios::binary };
		if (!input.is_open()) {
			return std::nullopt;
		}

		uint32_t magic{};
		uint32_t version{};
		uint32_t count{};
		input.read(reinterpret_cast<char*>(&magic), sizeof(uint32_t));
		input.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
		input.read(reinterpret_cast<char*>(&count), sizeof(uint32_t));
		if (magic != 0x30444853 || version != 1) {
			return std::nullopt;
		}

		CompiledShaderCollection collection{};
		for (uint32_t index{ 0 }; index < count; ++index) {
			std::string identifier{ ReadString(input) };
			uint64_t byteCodeSize{};
			input.read(reinterpret_cast<char*>(&byteCodeSize), sizeof(uint64_t));
			std::vector<uint8_t> byteCode(static_cast<std::size_t>(byteCodeSize));
			if (byteCodeSize > 0) {
				input.read(reinterpret_cast<char*>(byteCode.data()), static_cast<std::streamsize>(byteCodeSize));
			}
			collection.ByteCodesByIdentifier[identifier] = std::move(byteCode);
		}

		if (!input.good() && !input.eof()) {
			return std::nullopt;
		}

		return collection;
	}

	std::unordered_map<std::wstring, CompiledShaderCollection>& GetCompiledStorage() {
		static std::unordered_map<std::wstring, CompiledShaderCollection> compiledStorage{};
		return compiledStorage;
	}
}


Shader::Shader(const Shader& other)
	: mSourceFileName(other.mSourceFileName),
	mShaderByteCodes(other.mShaderByteCodes),
	mIdentifierToIndex(other.mIdentifierToIndex) {
}

Shader& Shader::operator=(const Shader& other) {
	if (this == &other) {
		return *this;
	}

	mSourceFileName = other.mSourceFileName;
	mShaderByteCodes = other.mShaderByteCodes;
	mIdentifierToIndex = other.mIdentifierToIndex;
	return *this;
}

Shader::Shader(Shader&& other) noexcept
	: mSourceFileName(std::move(other.mSourceFileName)),
	mShaderByteCodes(std::move(other.mShaderByteCodes)),
	mIdentifierToIndex(std::move(other.mIdentifierToIndex)) {
}

Shader& Shader::operator=(Shader&& other) noexcept {
	if (this == &other) {
		return *this;
	}

	mSourceFileName = std::move(other.mSourceFileName);
	mShaderByteCodes = std::move(other.mShaderByteCodes);
	mIdentifierToIndex = std::move(other.mIdentifierToIndex);
	return *this;
}

void Shader::Initialize(const fs::path& sourceFileName) {
	mSourceFileName = sourceFileName.wstring();
	InitializeFromCompiledStorage();
}

const std::wstring& Shader::GetSourceFileName() const {
	return mSourceFileName;
}

bool Shader::HasByteCode(const std::string& identifier) const {
	return mIdentifierToIndex.contains(identifier);
}

D3D12_SHADER_BYTECODE Shader::GetByteCode(const std::string& identifier) const {
	auto foundIterator{ mIdentifierToIndex.find(identifier) };
	if (foundIterator == mIdentifierToIndex.end()) {
		return D3D12_SHADER_BYTECODE{};
	}

	const ShaderByteCode& shaderByteCode{ mShaderByteCodes[foundIterator->second] };
	D3D12_SHADER_BYTECODE result{};
	result.BytecodeLength = shaderByteCode.ByteCode.size();
	result.pShaderBytecode = shaderByteCode.ByteCode.data();
	return result;
}

const std::vector<Shader::ShaderByteCode>& Shader::GetAllByteCodes() const {
	return mShaderByteCodes;
}

void Shader::InitializeFromCompiledStorage() {
	mShaderByteCodes.clear();
	mIdentifierToIndex.clear();
	auto storageIterator{ GetCompiledStorage().find(mSourceFileName) };
	if (storageIterator == GetCompiledStorage().end()) {
		return;
	}

	for (const auto& pair : storageIterator->second.ByteCodesByIdentifier) {
		ShaderByteCode shaderByteCode{};
		shaderByteCode.Identifier = pair.first;
		shaderByteCode.ByteCode = pair.second;
		std::size_t newIndex{ mShaderByteCodes.size() };
		mIdentifierToIndex[shaderByteCode.Identifier] = newIndex;
		mShaderByteCodes.emplace_back(std::move(shaderByteCode));
	}
}


namespace Game {
	namespace Base {
		bool PreCompileShaders() {
			std::unordered_map<std::wstring, std::vector<ShaderCompileOption>> optionsBySource{ ParseMetadataFile() };
			if (optionsBySource.empty()) {
				return true;
			}

			ComPtr<IDxcUtils> dxcUtils{ nullptr };
			ComPtr<IDxcCompiler3> dxcCompiler{ nullptr };
			HRESULT utilsResult{ DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(dxcUtils.GetAddressOf())) };
			HRESULT compilerResult{ DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(dxcCompiler.GetAddressOf())) };
			if (FAILED(utilsResult) || FAILED(compilerResult) || dxcUtils == nullptr || dxcCompiler == nullptr) {
				return false;
			}

			ComPtr<IDxcIncludeHandler> includeHandler{ nullptr };
			if (FAILED(dxcUtils->CreateDefaultIncludeHandler(includeHandler.GetAddressOf())) || includeHandler == nullptr) {
				return false;
			}

			std::unordered_map<std::wstring, CompiledShaderCollection> newStorage{};
			for (const auto& sourcePair : optionsBySource) {
				const std::wstring& sourceFileName{ sourcePair.first };
				CompiledShaderCollection collection{};
				if (!NeedsRecompile(sourceFileName)) {
					std::optional<CompiledShaderCollection> loadedCollection{ LoadCompiledBinary(sourceFileName) };
					if (loadedCollection.has_value()) {
						newStorage[sourceFileName] = std::move(loadedCollection.value());
						continue;
					}
				}

				bool compileSucceeded{ true };
				for (const ShaderCompileOption& option : sourcePair.second) {
					std::vector<uint8_t> byteCode{ CompileShader(option, dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get()) };
					if (byteCode.empty()) {
						compileSucceeded = false;
						break;
					}

					collection.ByteCodesByIdentifier[BuildIdentifier(option)] = std::move(byteCode);
				}

				if (!compileSucceeded) {
					return false;
				}

				if (!SaveCompiledBinary(sourceFileName, collection)) {
					return false;
				}

				newStorage[sourceFileName] = std::move(collection);
			}

			GetCompiledStorage() = std::move(newStorage);
			return true;
		}

	}
}

