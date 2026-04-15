#include "RootSignature.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>


#ifdef max
#undef max
#endif 

#ifdef min
#undef min
#endif 
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"

namespace {
	struct RootDescriptorTableParameterData {
		std::vector<D3D12_DESCRIPTOR_RANGE1> Ranges{};
	};

	struct RootParameterData {
		D3D12_ROOT_PARAMETER1 Parameter{};
		RootDescriptorTableParameterData DescriptorTableData{};
	};

	struct RootSignatureDescriptionData {
		std::vector<RootParameterData> Parameters{};
		std::vector<D3D12_STATIC_SAMPLER_DESC> StaticSamplers{};
		D3D12_ROOT_SIGNATURE_FLAGS Flags{};
	};

	std::string ReadRequiredString(const rapidjson::Value& object, const char* key) {
		if (object.HasMember(key) == false || object[key].IsString() == false) {
			return {};
		}

		return object[key].GetString();
	}

	uint32_t ReadOptionalUint(const rapidjson::Value& object, const char* key, uint32_t defaultValue) {
		if (object.HasMember(key) == false || object[key].IsUint() == false) {
			return defaultValue;
		}

		return object[key].GetUint();
	}

	int32_t ReadOptionalInt(const rapidjson::Value& object, const char* key, int32_t defaultValue) {
		if (object.HasMember(key) == false || object[key].IsInt() == false) {
			return defaultValue;
		}

		return object[key].GetInt();
	}

	float ReadOptionalFloat(const rapidjson::Value& object, const char* key, float defaultValue) {
		if (object.HasMember(key) == false || object[key].IsNumber() == false) {
			return defaultValue;
		}

		return object[key].GetFloat();
	}

	bool ReadOptionalBool(const rapidjson::Value& object, const char* key, bool defaultValue) {
		if (object.HasMember(key) == false || object[key].IsBool() == false) {
			return defaultValue;
		}

		return object[key].GetBool();
	}

	D3D12_SHADER_VISIBILITY ParseShaderVisibility(const std::string& value) {
		if (value == "Vertex") {
			return D3D12_SHADER_VISIBILITY_VERTEX;
		}

		if (value == "Hull") {
			return D3D12_SHADER_VISIBILITY_HULL;
		}

		if (value == "Domain") {
			return D3D12_SHADER_VISIBILITY_DOMAIN;
		}

		if (value == "Geometry") {
			return D3D12_SHADER_VISIBILITY_GEOMETRY;
		}

		if (value == "Pixel") {
			return D3D12_SHADER_VISIBILITY_PIXEL;
		}

		if (value == "Amplification") {
			return D3D12_SHADER_VISIBILITY_AMPLIFICATION;
		}

		if (value == "Mesh") {
			return D3D12_SHADER_VISIBILITY_MESH;
		}

		return D3D12_SHADER_VISIBILITY_ALL;
	}

	D3D12_ROOT_PARAMETER_TYPE ParseRootParameterType(const std::string& value) {
		if (value == "Constants") {
			return D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		}

		if (value == "CBV") {
			return D3D12_ROOT_PARAMETER_TYPE_CBV;
		}

		if (value == "SRV") {
			return D3D12_ROOT_PARAMETER_TYPE_SRV;
		}

		if (value == "UAV") {
			return D3D12_ROOT_PARAMETER_TYPE_UAV;
		}

		return D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	}

	D3D12_DESCRIPTOR_RANGE_TYPE ParseDescriptorRangeType(const std::string& value) {
		if (value == "SRV") {
			return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		}

		if (value == "UAV") {
			return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		}

		if (value == "CBV") {
			return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		}

		if (value == "Sampler") {
			return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
		}

		return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	}

	D3D12_DESCRIPTOR_RANGE_FLAGS ParseDescriptorRangeFlags(const rapidjson::Value& arrayValue) {
		D3D12_DESCRIPTOR_RANGE_FLAGS flags{ D3D12_DESCRIPTOR_RANGE_FLAG_NONE };
		if (arrayValue.IsArray() == false) {
			return flags;
		}

		for (const rapidjson::Value& entry : arrayValue.GetArray()) {
			if (entry.IsString() == false) {
				continue;
			}

			std::string text{ entry.GetString() };
			if (text == "DescriptorsVolatile") {
				flags = static_cast<D3D12_DESCRIPTOR_RANGE_FLAGS>(flags | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
			}
			else if (text == "DataVolatile") {
				flags = static_cast<D3D12_DESCRIPTOR_RANGE_FLAGS>(flags | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
			}
			else if (text == "DataStaticWhileSetAtExecute") {
				flags = static_cast<D3D12_DESCRIPTOR_RANGE_FLAGS>(flags | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE);
			}
			else if (text == "DataStatic") {
				flags = static_cast<D3D12_DESCRIPTOR_RANGE_FLAGS>(flags | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
			}
			else if (text == "DescriptorsStaticKeepingBufferBoundsChecks") {
				flags = static_cast<D3D12_DESCRIPTOR_RANGE_FLAGS>(flags | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_STATIC_KEEPING_BUFFER_BOUNDS_CHECKS);
			}
		}

		return flags;
	}

	D3D12_ROOT_DESCRIPTOR_FLAGS ParseRootDescriptorFlags(const rapidjson::Value& arrayValue) {
		D3D12_ROOT_DESCRIPTOR_FLAGS flags{ D3D12_ROOT_DESCRIPTOR_FLAG_NONE };
		if (arrayValue.IsArray() == false) {
			return flags;
		}

		for (const rapidjson::Value& entry : arrayValue.GetArray()) {
			if (entry.IsString() == false) {
				continue;
			}

			std::string text{ entry.GetString() };
			if (text == "DataVolatile") {
				flags = static_cast<D3D12_ROOT_DESCRIPTOR_FLAGS>(flags | D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE);
			}
			else if (text == "DataStaticWhileSetAtExecute") {
				flags = static_cast<D3D12_ROOT_DESCRIPTOR_FLAGS>(flags | D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE);
			}
			else if (text == "DataStatic") {
				flags = static_cast<D3D12_ROOT_DESCRIPTOR_FLAGS>(flags | D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);
			}
		}

		return flags;
	}

	D3D12_ROOT_SIGNATURE_FLAGS ParseRootSignatureFlags(const rapidjson::Value& arrayValue) {
		D3D12_ROOT_SIGNATURE_FLAGS flags{ D3D12_ROOT_SIGNATURE_FLAG_NONE };
		if (arrayValue.IsArray() == false) {
			return flags;
		}

		for (const rapidjson::Value& entry : arrayValue.GetArray()) {
			if (entry.IsString() == false) {
				continue;
			}

			std::string text{ entry.GetString() };
			if (text == "AllowInputAssemblerInputLayout") {
				flags = static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(flags | D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
			}
			else if (text == "DenyVertexShaderRootAccess") {
				flags = static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(flags | D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS);
			}
			else if (text == "DenyHullShaderRootAccess") {
				flags = static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(flags | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS);
			}
			else if (text == "DenyDomainShaderRootAccess") {
				flags = static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(flags | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS);
			}
			else if (text == "DenyGeometryShaderRootAccess") {
				flags = static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(flags | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS);
			}
			else if (text == "DenyPixelShaderRootAccess") {
				flags = static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(flags | D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS);
			}
			else if (text == "AllowStreamOutput") {
				flags = static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(flags | D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT);
			}
			else if (text == "LocalRootSignature") {
				flags = static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(flags | D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);
			}
			else if (text == "DenyAmplificationShaderRootAccess") {
				flags = static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(flags | D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS);
			}
			else if (text == "DenyMeshShaderRootAccess") {
				flags = static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(flags | D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS);
			}
			else if (text == "CBVSRVUAVHeapDirectlyIndexed") {
				flags = static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(flags | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
			}
			else if (text == "SamplerHeapDirectlyIndexed") {
				flags = static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(flags | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED);
			}
		}

		return flags;
	}

	D3D12_FILTER ParseFilter(const std::string& value) {
		if (value == "MinMagMipPoint") {
			return D3D12_FILTER_MIN_MAG_MIP_POINT;
		}

		if (value == "MinMagPointMipLinear") {
			return D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR;
		}

		if (value == "MinPointMagLinearMipPoint") {
			return D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
		}

		if (value == "MinPointMagMipLinear") {
			return D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR;
		}

		if (value == "MinLinearMagMipPoint") {
			return D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
		}

		if (value == "MinLinearMagPointMipLinear") {
			return D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
		}

		if (value == "MinMagLinearMipPoint") {
			return D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
		}

		if (value == "Anisotropic") {
			return D3D12_FILTER_ANISOTROPIC;
		}

		if (value == "ComparisonMinMagMipPoint") {
			return D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
		}

		if (value == "ComparisonMinMagPointMipLinear") {
			return D3D12_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR;
		}

		if (value == "ComparisonMinPointMagLinearMipPoint") {
			return D3D12_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT;
		}

		if (value == "ComparisonMinPointMagMipLinear") {
			return D3D12_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR;
		}

		if (value == "ComparisonMinLinearMagMipPoint") {
			return D3D12_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT;
		}

		if (value == "ComparisonMinLinearMagPointMipLinear") {
			return D3D12_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
		}

		if (value == "ComparisonMinMagLinearMipPoint") {
			return D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
		}

		if (value == "ComparisonMinMagMipLinear") {
			return D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
		}

		if (value == "ComparisonAnisotropic") {
			return D3D12_FILTER_COMPARISON_ANISOTROPIC;
		}

		return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	}

	D3D12_TEXTURE_ADDRESS_MODE ParseTextureAddressMode(const std::string& value) {
		if (value == "Mirror") {
			return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
		}

		if (value == "Clamp") {
			return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		}

		if (value == "Border") {
			return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		}

		if (value == "MirrorOnce") {
			return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
		}

		return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	}

	D3D12_COMPARISON_FUNC ParseComparisonFunc(const std::string& value) {
		if (value == "Never") {
			return D3D12_COMPARISON_FUNC_NEVER;
		}

		if (value == "Less") {
			return D3D12_COMPARISON_FUNC_LESS;
		}

		if (value == "Equal") {
			return D3D12_COMPARISON_FUNC_EQUAL;
		}

		if (value == "LessEqual") {
			return D3D12_COMPARISON_FUNC_LESS_EQUAL;
		}

		if (value == "Greater") {
			return D3D12_COMPARISON_FUNC_GREATER;
		}

		if (value == "NotEqual") {
			return D3D12_COMPARISON_FUNC_NOT_EQUAL;
		}

		if (value == "GreaterEqual") {
			return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
		}

		return D3D12_COMPARISON_FUNC_ALWAYS;
	}

	D3D12_STATIC_BORDER_COLOR ParseStaticBorderColor(const std::string& value) {
		if (value == "TransparentBlack") {
			return D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		}

		if (value == "OpaqueBlack") {
			return D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		}

		if (value == "OpaqueWhite") {
			return D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
		}

		return D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	}

	bool ParseRootSignatureDescription(const rapidjson::Document& document, RootSignatureDescriptionData& descriptionData) {
		if (document.IsObject() == false) {
			return false;
		}

		descriptionData.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
		if (document.HasMember("Flags") == true) {
			descriptionData.Flags = ParseRootSignatureFlags(document["Flags"]);
		}

		if (document.HasMember("Parameters") == false || document["Parameters"].IsArray() == false) {
			return true;
		}

		for (const rapidjson::Value& parameterValue : document["Parameters"].GetArray()) {
			if (parameterValue.IsObject() == false) {
				continue;
			}

			RootParameterData parameterData{};
			parameterData.Parameter.ShaderVisibility = ParseShaderVisibility(ReadRequiredString(parameterValue, "ShaderVisibility"));
			parameterData.Parameter.ParameterType = ParseRootParameterType(ReadRequiredString(parameterValue, "Type"));

			if (parameterData.Parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
				if (parameterValue.HasMember("DescriptorTable") == false || parameterValue["DescriptorTable"].IsObject() == false) {
					return false;
				}

				const rapidjson::Value& descriptorTable{ parameterValue["DescriptorTable"] };
				if (descriptorTable.HasMember("Ranges") == false || descriptorTable["Ranges"].IsArray() == false) {
					return false;
				}

				for (const rapidjson::Value& rangeValue : descriptorTable["Ranges"].GetArray()) {
					if (rangeValue.IsObject() == false) {
						continue;
					}

					D3D12_DESCRIPTOR_RANGE1 range{};
					range.RangeType = ParseDescriptorRangeType(ReadRequiredString(rangeValue, "RangeType"));
					range.NumDescriptors = ReadOptionalUint(rangeValue, "NumDescriptors", 1);
					range.BaseShaderRegister = ReadOptionalUint(rangeValue, "BaseShaderRegister", 0);
					range.RegisterSpace = ReadOptionalUint(rangeValue, "RegisterSpace", 0);
					range.OffsetInDescriptorsFromTableStart = ReadOptionalUint(rangeValue, "OffsetInDescriptorsFromTableStart", D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND);
					range.Flags = rangeValue.HasMember("Flags") == true ? ParseDescriptorRangeFlags(rangeValue["Flags"]) : D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
					parameterData.DescriptorTableData.Ranges.emplace_back(range);
				}

				parameterData.Parameter.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(parameterData.DescriptorTableData.Ranges.size());
				parameterData.Parameter.DescriptorTable.pDescriptorRanges = parameterData.DescriptorTableData.Ranges.data();
			}
			else if (parameterData.Parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS) {
				if (parameterValue.HasMember("Constants") == false || parameterValue["Constants"].IsObject() == false) {
					return false;
				}

				const rapidjson::Value& constantsValue{ parameterValue["Constants"] };
				parameterData.Parameter.Constants.ShaderRegister = ReadOptionalUint(constantsValue, "ShaderRegister", 0);
				parameterData.Parameter.Constants.RegisterSpace = ReadOptionalUint(constantsValue, "RegisterSpace", 0);
				parameterData.Parameter.Constants.Num32BitValues = ReadOptionalUint(constantsValue, "Num32BitValues", 1);
			}
			else {
				if (parameterValue.HasMember("Descriptor") == false || parameterValue["Descriptor"].IsObject() == false) {
					return false;
				}

				const rapidjson::Value& descriptorValue{ parameterValue["Descriptor"] };
				parameterData.Parameter.Descriptor.ShaderRegister = ReadOptionalUint(descriptorValue, "ShaderRegister", 0);
				parameterData.Parameter.Descriptor.RegisterSpace = ReadOptionalUint(descriptorValue, "RegisterSpace", 0);
				parameterData.Parameter.Descriptor.Flags = descriptorValue.HasMember("Flags") == true ? ParseRootDescriptorFlags(descriptorValue["Flags"]) : D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
			}

			descriptionData.Parameters.emplace_back(std::move(parameterData));
		}

		if (document.HasMember("StaticSamplers") == false || document["StaticSamplers"].IsArray() == false) {
			return true;
		}

		for (const rapidjson::Value& samplerValue : document["StaticSamplers"].GetArray()) {
			if (samplerValue.IsObject() == false) {
				continue;
			}

			D3D12_STATIC_SAMPLER_DESC samplerDesc{};
			samplerDesc.Filter = ParseFilter(ReadRequiredString(samplerValue, "Filter"));
			samplerDesc.AddressU = ParseTextureAddressMode(ReadRequiredString(samplerValue, "AddressU"));
			samplerDesc.AddressV = ParseTextureAddressMode(ReadRequiredString(samplerValue, "AddressV"));
			samplerDesc.AddressW = ParseTextureAddressMode(ReadRequiredString(samplerValue, "AddressW"));
			samplerDesc.MipLODBias = ReadOptionalFloat(samplerValue, "MipLODBias", 0.0f);
			samplerDesc.MaxAnisotropy = ReadOptionalUint(samplerValue, "MaxAnisotropy", 1);
			samplerDesc.ComparisonFunc = ParseComparisonFunc(ReadRequiredString(samplerValue, "ComparisonFunc"));
			samplerDesc.BorderColor = ParseStaticBorderColor(ReadRequiredString(samplerValue, "BorderColor"));
			samplerDesc.MinLOD = ReadOptionalFloat(samplerValue, "MinLOD", 0.0f);
			samplerDesc.MaxLOD = ReadOptionalFloat(samplerValue, "MaxLOD", D3D12_FLOAT32_MAX);
			samplerDesc.ShaderRegister = ReadOptionalUint(samplerValue, "ShaderRegister", 0);
			samplerDesc.RegisterSpace = ReadOptionalUint(samplerValue, "RegisterSpace", 0);
			samplerDesc.ShaderVisibility = ParseShaderVisibility(ReadRequiredString(samplerValue, "ShaderVisibility"));
			descriptionData.StaticSamplers.emplace_back(samplerDesc);
		}

		return true;
	}

	bool CreateRootSignatureFromFile(ID3D12Device* device, const std::filesystem::path& jsonPath, std::unordered_map<std::string, ComPtr<ID3D12RootSignature>>& rootSignatures) {
		std::ifstream input{ jsonPath, std::ios::binary };
		if (input.is_open() == false) {
			return false;
		}

		std::string jsonText{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
		if (jsonText.empty() == true) {
			return false;
		}

		if (jsonText.size() >= 3 && static_cast<unsigned char>(jsonText[0]) == 0xEF && static_cast<unsigned char>(jsonText[1]) == 0xBB && static_cast<unsigned char>(jsonText[2]) == 0xBF) {
			jsonText.erase(0, 3);
		}

		rapidjson::StringStream stream{ jsonText.c_str() };
		rapidjson::Document document{};
		document.ParseStream<rapidjson::kParseCommentsFlag | rapidjson::kParseTrailingCommasFlag>(stream);

		if (document.HasParseError() == true || document.IsObject() == false) {
			return false;
		}

		std::string rootSignatureName{ ReadRequiredString(document, "Name") };
		if (rootSignatureName.empty() == true) {
			rootSignatureName = jsonPath.stem().string();
		}

		if (rootSignatures.contains(rootSignatureName) == true) {
			return false;
		}

		RootSignatureDescriptionData descriptionData{};
		if (ParseRootSignatureDescription(document, descriptionData) == false) {
			return false;
		}

		std::vector<D3D12_ROOT_PARAMETER1> parameters{};
		parameters.reserve(descriptionData.Parameters.size());
		for (const RootParameterData& parameterData : descriptionData.Parameters) {
			parameters.emplace_back(parameterData.Parameter);
		}

		D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionedDesc{};
		versionedDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
		versionedDesc.Desc_1_1.NumParameters = static_cast<UINT>(parameters.size());
		versionedDesc.Desc_1_1.pParameters = parameters.empty() == true ? nullptr : parameters.data();
		versionedDesc.Desc_1_1.NumStaticSamplers = static_cast<UINT>(descriptionData.StaticSamplers.size());
		versionedDesc.Desc_1_1.pStaticSamplers = descriptionData.StaticSamplers.empty() == true ? nullptr : descriptionData.StaticSamplers.data();
		versionedDesc.Desc_1_1.Flags = descriptionData.Flags;

		ComPtr<ID3DBlob> serializedBlob{ nullptr };
		ComPtr<ID3DBlob> errorBlob{ nullptr };
		HRESULT serializeResult{ D3D12SerializeVersionedRootSignature(&versionedDesc, serializedBlob.GetAddressOf(), errorBlob.GetAddressOf()) };
		if (FAILED(serializeResult) == true || serializedBlob == nullptr) {
			return false;
		}

		ComPtr<ID3D12RootSignature> rootSignature{ nullptr };
		HRESULT createResult{ device->CreateRootSignature(0, serializedBlob->GetBufferPointer(), serializedBlob->GetBufferSize(), IID_PPV_ARGS(rootSignature.GetAddressOf())) };
		if (FAILED(createResult) == true || rootSignature == nullptr) {
			return false;
		}

		rootSignatures[rootSignatureName] = rootSignature;
		return true;
	}

	std::unordered_map<std::string, ComPtr<ID3D12RootSignature>>& GetCompiledRootSignaturesStorage() {
		static std::unordered_map<std::string, ComPtr<ID3D12RootSignature>> rootSignatures{};
		return rootSignatures;
	}

	std::mutex& GetCompiledRootSignaturesMutex() {
		static std::mutex mutex{};
		return mutex;
	}
}

namespace Game {
	namespace Base {
		bool RootSignature::Initialize(const std::string& rootSignatureName) {
			if (rootSignatureName.empty() == true) {
				return false;
			}

			std::scoped_lock<std::mutex> lock{ GetCompiledRootSignaturesMutex() };

			const std::unordered_map<std::string, ComPtr<ID3D12RootSignature>>& rootSignatures{ GetCompiledRootSignaturesStorage() };
			auto iterator{ rootSignatures.find(rootSignatureName) };
			if (iterator == rootSignatures.end()) {
				return false;
			}

			mRootSignature = iterator->second;
			return true;
		}

		ID3D12RootSignature* RootSignature::Get() const {
			return mRootSignature.Get();
		}

		bool RootSignature::HasCompiledRootSignature(const std::string& rootSignatureName) {
			std::scoped_lock<std::mutex> lock{ GetCompiledRootSignaturesMutex() };
			const std::unordered_map<std::string, ComPtr<ID3D12RootSignature>>& rootSignatures{ GetCompiledRootSignaturesStorage() };
			return rootSignatures.contains(rootSignatureName);
		}

		bool PreCompileRootSignatures(ID3D12Device* device) {
			if (device == nullptr) {
				return false;
			}

			std::filesystem::path folderPath{ std::filesystem::current_path() / "Shader" / "RS" };
			if (std::filesystem::exists(folderPath) == false) {
				return true;
			}

			std::unordered_map<std::string, ComPtr<ID3D12RootSignature>> rootSignatures{};
			for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folderPath)) {
				if (entry.is_regular_file() == false || entry.path().extension() != ".json") {
					continue;
				}

				std::string stem{ entry.path().stem().string() };
				if (stem.rfind("Schema", 0) == 0) {
					continue;
				}

				if (CreateRootSignatureFromFile(device, entry.path(), rootSignatures) == false) {
					return false;
				}
			}

			std::scoped_lock<std::mutex> lock{ GetCompiledRootSignaturesMutex() };
			GetCompiledRootSignaturesStorage() = std::move(rootSignatures);
			return true;
		}

	}
}
