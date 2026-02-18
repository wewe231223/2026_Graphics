#include "Pipeline.h"
#include <array>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "Utility/ErrorHandler.h"
#include "Shader.h"

#ifdef max
#undef max
#endif 

#ifdef min
#undef min
#endif 
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/istreamwrapper.h"

namespace {
	struct PipelineInputLayoutData {
		std::deque<std::string> SemanticNames{};
		std::vector<D3D12_INPUT_ELEMENT_DESC> Elements{};
	};

	struct PipelineStreamOutputData {
		std::deque<std::string> SemanticNames{};
		std::vector<D3D12_SO_DECLARATION_ENTRY> Entries{};
		std::vector<UINT> BufferStrides{};
	};

	struct PipelineShaderData {
		std::string Source{};
		std::string Identifier{};
	};

	struct PipelineDescriptionData {
		std::string Name{};
		std::string Type{};
		std::string RootSignatureName{};

		PipelineShaderData Vs{};
		PipelineShaderData Ps{};
		PipelineShaderData Ds{};
		PipelineShaderData Hs{};
		PipelineShaderData Gs{};
		PipelineShaderData Cs{};

		PipelineInputLayoutData InputLayout{};
		PipelineStreamOutputData StreamOutput{};

		D3D12_BLEND_DESC BlendState{};
		UINT SampleMask{};
		D3D12_RASTERIZER_DESC RasterizerState{};
		D3D12_DEPTH_STENCIL_DESC DepthStencilState{};
		D3D12_INDEX_BUFFER_STRIP_CUT_VALUE IBStripCutValue{};
		D3D12_PRIMITIVE_TOPOLOGY_TYPE PrimitiveTopologyType{};
		std::array<DXGI_FORMAT, 8> RtvFormats{};
		UINT NumRenderTargets{};
		DXGI_FORMAT DsvFormat{};
		DXGI_SAMPLE_DESC SampleDesc{};
		UINT NodeMask{};
		D3D12_CACHED_PIPELINE_STATE CachedPso{};
		D3D12_PIPELINE_STATE_FLAGS Flags{};
	};

	struct CompiledPipelineData {
		ComPtr<ID3D12RootSignature> RootSignature{};
		ComPtr<ID3D12PipelineState> PipelineState{};
	};

	std::unordered_map<std::string, CompiledPipelineData>& GetCompiledPipelinesStorage() {
		static std::unordered_map<std::string, CompiledPipelineData> pipelines{};
		return pipelines;
	}

	std::mutex& GetCompiledPipelinesMutex() {
		static std::mutex mutex{};
		return mutex;
	}

	std::string ReadOptionalString(const rapidjson::Value& object, const char* key, const std::string& defaultValue) {
		if (object.HasMember(key) == false || object[key].IsString() == false) {
			return defaultValue;
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

	bool ReadOptionalBool(const rapidjson::Value& object, const char* key, bool defaultValue) {
		if (object.HasMember(key) == false || object[key].IsBool() == false) {
			return defaultValue;
		}

		return object[key].GetBool();
	}

	float ReadOptionalFloat(const rapidjson::Value& object, const char* key, float defaultValue) {
		if (object.HasMember(key) == false || object[key].IsNumber() == false) {
			return defaultValue;
		}

		return object[key].GetFloat();
	}

	DXGI_FORMAT ParseFormat(const std::string& value) {
		static const std::unordered_map<std::string, DXGI_FORMAT> FormatTable{
			{ "UNKNOWN", DXGI_FORMAT_UNKNOWN },
			{ "R32G32B32A32_TYPELESS", DXGI_FORMAT_R32G32B32A32_TYPELESS },
			{ "R32G32B32A32_FLOAT", DXGI_FORMAT_R32G32B32A32_FLOAT },
			{ "R32G32B32A32_UINT", DXGI_FORMAT_R32G32B32A32_UINT },
			{ "R32G32B32A32_SINT", DXGI_FORMAT_R32G32B32A32_SINT },
			{ "R32G32B32_TYPELESS", DXGI_FORMAT_R32G32B32_TYPELESS },
			{ "R32G32B32_FLOAT", DXGI_FORMAT_R32G32B32_FLOAT },
			{ "R32G32B32_UINT", DXGI_FORMAT_R32G32B32_UINT },
			{ "R32G32B32_SINT", DXGI_FORMAT_R32G32B32_SINT },
			{ "R16G16B16A16_TYPELESS", DXGI_FORMAT_R16G16B16A16_TYPELESS },
			{ "R16G16B16A16_FLOAT", DXGI_FORMAT_R16G16B16A16_FLOAT },
			{ "R16G16B16A16_UNORM", DXGI_FORMAT_R16G16B16A16_UNORM },
			{ "R16G16B16A16_UINT", DXGI_FORMAT_R16G16B16A16_UINT },
			{ "R16G16B16A16_SNORM", DXGI_FORMAT_R16G16B16A16_SNORM },
			{ "R16G16B16A16_SINT", DXGI_FORMAT_R16G16B16A16_SINT },
			{ "R32G32_TYPELESS", DXGI_FORMAT_R32G32_TYPELESS },
			{ "R32G32_FLOAT", DXGI_FORMAT_R32G32_FLOAT },
			{ "R32G32_UINT", DXGI_FORMAT_R32G32_UINT },
			{ "R32G32_SINT", DXGI_FORMAT_R32G32_SINT },
			{ "R32G8X24_TYPELESS", DXGI_FORMAT_R32G8X24_TYPELESS },
			{ "D32_FLOAT_S8X24_UINT", DXGI_FORMAT_D32_FLOAT_S8X24_UINT },
			{ "R32_FLOAT_X8X24_TYPELESS", DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS },
			{ "X32_TYPELESS_G8X24_UINT", DXGI_FORMAT_X32_TYPELESS_G8X24_UINT },
			{ "R10G10B10A2_TYPELESS", DXGI_FORMAT_R10G10B10A2_TYPELESS },
			{ "R10G10B10A2_UNORM", DXGI_FORMAT_R10G10B10A2_UNORM },
			{ "R10G10B10A2_UINT", DXGI_FORMAT_R10G10B10A2_UINT },
			{ "R11G11B10_FLOAT", DXGI_FORMAT_R11G11B10_FLOAT },
			{ "R8G8B8A8_TYPELESS", DXGI_FORMAT_R8G8B8A8_TYPELESS },
			{ "R8G8B8A8_UNORM", DXGI_FORMAT_R8G8B8A8_UNORM },
			{ "R8G8B8A8_UNORM_SRGB", DXGI_FORMAT_R8G8B8A8_UNORM_SRGB },
			{ "R8G8B8A8_UINT", DXGI_FORMAT_R8G8B8A8_UINT },
			{ "R8G8B8A8_SNORM", DXGI_FORMAT_R8G8B8A8_SNORM },
			{ "R8G8B8A8_SINT", DXGI_FORMAT_R8G8B8A8_SINT },
			{ "R16G16_TYPELESS", DXGI_FORMAT_R16G16_TYPELESS },
			{ "R16G16_FLOAT", DXGI_FORMAT_R16G16_FLOAT },
			{ "R16G16_UNORM", DXGI_FORMAT_R16G16_UNORM },
			{ "R16G16_UINT", DXGI_FORMAT_R16G16_UINT },
			{ "R16G16_SNORM", DXGI_FORMAT_R16G16_SNORM },
			{ "R16G16_SINT", DXGI_FORMAT_R16G16_SINT },
			{ "R32_TYPELESS", DXGI_FORMAT_R32_TYPELESS },
			{ "D32_FLOAT", DXGI_FORMAT_D32_FLOAT },
			{ "R32_FLOAT", DXGI_FORMAT_R32_FLOAT },
			{ "R32_UINT", DXGI_FORMAT_R32_UINT },
			{ "R32_SINT", DXGI_FORMAT_R32_SINT },
			{ "R24G8_TYPELESS", DXGI_FORMAT_R24G8_TYPELESS },
			{ "D24_UNORM_S8_UINT", DXGI_FORMAT_D24_UNORM_S8_UINT },
			{ "R24_UNORM_X8_TYPELESS", DXGI_FORMAT_R24_UNORM_X8_TYPELESS },
			{ "X24_TYPELESS_G8_UINT", DXGI_FORMAT_X24_TYPELESS_G8_UINT },
			{ "R8G8_TYPELESS", DXGI_FORMAT_R8G8_TYPELESS },
			{ "R8G8_UNORM", DXGI_FORMAT_R8G8_UNORM },
			{ "R8G8_UINT", DXGI_FORMAT_R8G8_UINT },
			{ "R8G8_SNORM", DXGI_FORMAT_R8G8_SNORM },
			{ "R8G8_SINT", DXGI_FORMAT_R8G8_SINT },
			{ "R16_TYPELESS", DXGI_FORMAT_R16_TYPELESS },
			{ "R16_FLOAT", DXGI_FORMAT_R16_FLOAT },
			{ "D16_UNORM", DXGI_FORMAT_D16_UNORM },
			{ "R16_UNORM", DXGI_FORMAT_R16_UNORM },
			{ "R16_UINT", DXGI_FORMAT_R16_UINT },
			{ "R16_SNORM", DXGI_FORMAT_R16_SNORM },
			{ "R16_SINT", DXGI_FORMAT_R16_SINT },
			{ "R8_TYPELESS", DXGI_FORMAT_R8_TYPELESS },
			{ "R8_UNORM", DXGI_FORMAT_R8_UNORM },
			{ "R8_UINT", DXGI_FORMAT_R8_UINT },
			{ "R8_SNORM", DXGI_FORMAT_R8_SNORM },
			{ "R8_SINT", DXGI_FORMAT_R8_SINT },
			{ "A8_UNORM", DXGI_FORMAT_A8_UNORM },
			{ "R1_UNORM", DXGI_FORMAT_R1_UNORM },
			{ "R9G9B9E5_SHAREDEXP", DXGI_FORMAT_R9G9B9E5_SHAREDEXP },
			{ "R8G8_B8G8_UNORM", DXGI_FORMAT_R8G8_B8G8_UNORM },
			{ "G8R8_G8B8_UNORM", DXGI_FORMAT_G8R8_G8B8_UNORM },
			{ "BC1_TYPELESS", DXGI_FORMAT_BC1_TYPELESS },
			{ "BC1_UNORM", DXGI_FORMAT_BC1_UNORM },
			{ "BC1_UNORM_SRGB", DXGI_FORMAT_BC1_UNORM_SRGB },
			{ "BC2_TYPELESS", DXGI_FORMAT_BC2_TYPELESS },
			{ "BC2_UNORM", DXGI_FORMAT_BC2_UNORM },
			{ "BC2_UNORM_SRGB", DXGI_FORMAT_BC2_UNORM_SRGB },
			{ "BC3_TYPELESS", DXGI_FORMAT_BC3_TYPELESS },
			{ "BC3_UNORM", DXGI_FORMAT_BC3_UNORM },
			{ "BC3_UNORM_SRGB", DXGI_FORMAT_BC3_UNORM_SRGB },
			{ "BC4_TYPELESS", DXGI_FORMAT_BC4_TYPELESS },
			{ "BC4_UNORM", DXGI_FORMAT_BC4_UNORM },
			{ "BC4_SNORM", DXGI_FORMAT_BC4_SNORM },
			{ "BC5_TYPELESS", DXGI_FORMAT_BC5_TYPELESS },
			{ "BC5_UNORM", DXGI_FORMAT_BC5_UNORM },
			{ "BC5_SNORM", DXGI_FORMAT_BC5_SNORM },
			{ "B5G6R5_UNORM", DXGI_FORMAT_B5G6R5_UNORM },
			{ "B5G5R5A1_UNORM", DXGI_FORMAT_B5G5R5A1_UNORM },
			{ "B8G8R8A8_UNORM", DXGI_FORMAT_B8G8R8A8_UNORM },
			{ "B8G8R8X8_UNORM", DXGI_FORMAT_B8G8R8X8_UNORM },
			{ "R10G10B10_XR_BIAS_A2_UNORM", DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM },
			{ "B8G8R8A8_TYPELESS", DXGI_FORMAT_B8G8R8A8_TYPELESS },
			{ "B8G8R8A8_UNORM_SRGB", DXGI_FORMAT_B8G8R8A8_UNORM_SRGB },
			{ "B8G8R8X8_TYPELESS", DXGI_FORMAT_B8G8R8X8_TYPELESS },
			{ "B8G8R8X8_UNORM_SRGB", DXGI_FORMAT_B8G8R8X8_UNORM_SRGB },
			{ "BC6H_TYPELESS", DXGI_FORMAT_BC6H_TYPELESS },
			{ "BC6H_UF16", DXGI_FORMAT_BC6H_UF16 },
			{ "BC6H_SF16", DXGI_FORMAT_BC6H_SF16 },
			{ "BC7_TYPELESS", DXGI_FORMAT_BC7_TYPELESS },
			{ "BC7_UNORM", DXGI_FORMAT_BC7_UNORM },
			{ "BC7_UNORM_SRGB", DXGI_FORMAT_BC7_UNORM_SRGB },
			{ "AYUV", DXGI_FORMAT_AYUV },
			{ "Y410", DXGI_FORMAT_Y410 },
			{ "Y416", DXGI_FORMAT_Y416 },
			{ "NV12", DXGI_FORMAT_NV12 },
			{ "P010", DXGI_FORMAT_P010 },
			{ "P016", DXGI_FORMAT_P016 },
			{ "420_OPAQUE", DXGI_FORMAT_420_OPAQUE },
			{ "YUY2", DXGI_FORMAT_YUY2 },
			{ "Y210", DXGI_FORMAT_Y210 },
			{ "Y216", DXGI_FORMAT_Y216 },
			{ "NV11", DXGI_FORMAT_NV11 },
			{ "AI44", DXGI_FORMAT_AI44 },
			{ "IA44", DXGI_FORMAT_IA44 },
			{ "P8", DXGI_FORMAT_P8 },
			{ "A8P8", DXGI_FORMAT_A8P8 },
			{ "B4G4R4A4_UNORM", DXGI_FORMAT_B4G4R4A4_UNORM },
			{ "P208", DXGI_FORMAT_P208 },
			{ "V208", DXGI_FORMAT_V208 },
			{ "V408", DXGI_FORMAT_V408 },
			{ "SAMPLER_FEEDBACK_MIN_MIP_OPAQUE", DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE },
			{ "SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE", DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE }
		};

		auto iterator{ FormatTable.find(value) };
		if (iterator == FormatTable.end()) {
			return DXGI_FORMAT_UNKNOWN;
		}

		return iterator->second;
	}

	D3D12_INPUT_CLASSIFICATION ParseInputClassification(const std::string& value) {
		if (value == "PerInstanceData") {
			return D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
		}

		return D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
	}

	D3D12_FILL_MODE ParseFillMode(const std::string& value) {
		if (value == "Wireframe") {
			return D3D12_FILL_MODE_WIREFRAME;
		}

		return D3D12_FILL_MODE_SOLID;
	}

	D3D12_CULL_MODE ParseCullMode(const std::string& value) {
		if (value == "Front") {
			return D3D12_CULL_MODE_FRONT;
		}

		if (value == "None") {
			return D3D12_CULL_MODE_NONE;
		}

		return D3D12_CULL_MODE_BACK;
	}

	D3D12_CONSERVATIVE_RASTERIZATION_MODE ParseConservativeRaster(const std::string& value) {
		if (value == "On") {
			return D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON;
		}

		return D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
	}

	D3D12_BLEND ParseBlend(const std::string& value) {
		if (value == "Zero") {
			return D3D12_BLEND_ZERO;
		}

		if (value == "SrcColor") {
			return D3D12_BLEND_SRC_COLOR;
		}

		if (value == "InvSrcColor") {
			return D3D12_BLEND_INV_SRC_COLOR;
		}

		if (value == "SrcAlpha") {
			return D3D12_BLEND_SRC_ALPHA;
		}

		if (value == "InvSrcAlpha") {
			return D3D12_BLEND_INV_SRC_ALPHA;
		}

		if (value == "DestAlpha") {
			return D3D12_BLEND_DEST_ALPHA;
		}

		if (value == "InvDestAlpha") {
			return D3D12_BLEND_INV_DEST_ALPHA;
		}

		if (value == "DestColor") {
			return D3D12_BLEND_DEST_COLOR;
		}

		if (value == "InvDestColor") {
			return D3D12_BLEND_INV_DEST_COLOR;
		}

		if (value == "SrcAlphaSat") {
			return D3D12_BLEND_SRC_ALPHA_SAT;
		}

		if (value == "BlendFactor") {
			return D3D12_BLEND_BLEND_FACTOR;
		}

		if (value == "InvBlendFactor") {
			return D3D12_BLEND_INV_BLEND_FACTOR;
		}

		if (value == "Src1Color") {
			return D3D12_BLEND_SRC1_COLOR;
		}

		if (value == "InvSrc1Color") {
			return D3D12_BLEND_INV_SRC1_COLOR;
		}

		if (value == "Src1Alpha") {
			return D3D12_BLEND_SRC1_ALPHA;
		}

		if (value == "InvSrc1Alpha") {
			return D3D12_BLEND_INV_SRC1_ALPHA;
		}

		return D3D12_BLEND_ONE;
	}

	D3D12_BLEND_OP ParseBlendOp(const std::string& value) {
		if (value == "Subtract") {
			return D3D12_BLEND_OP_SUBTRACT;
		}

		if (value == "RevSubtract") {
			return D3D12_BLEND_OP_REV_SUBTRACT;
		}

		if (value == "Min") {
			return D3D12_BLEND_OP_MIN;
		}

		if (value == "Max") {
			return D3D12_BLEND_OP_MAX;
		}

		return D3D12_BLEND_OP_ADD;
	}

	D3D12_LOGIC_OP ParseLogicOp(const std::string& value) {
		if (value == "Clear") {
			return D3D12_LOGIC_OP_CLEAR;
		}

		if (value == "Set") {
			return D3D12_LOGIC_OP_SET;
		}

		if (value == "Copy") {
			return D3D12_LOGIC_OP_COPY;
		}

		if (value == "CopyInverted") {
			return D3D12_LOGIC_OP_COPY_INVERTED;
		}

		if (value == "Noop") {
			return D3D12_LOGIC_OP_NOOP;
		}

		if (value == "Invert") {
			return D3D12_LOGIC_OP_INVERT;
		}

		if (value == "And") {
			return D3D12_LOGIC_OP_AND;
		}

		if (value == "Nand") {
			return D3D12_LOGIC_OP_NAND;
		}

		if (value == "Or") {
			return D3D12_LOGIC_OP_OR;
		}

		if (value == "Nor") {
			return D3D12_LOGIC_OP_NOR;
		}

		if (value == "Xor") {
			return D3D12_LOGIC_OP_XOR;
		}

		if (value == "Equiv") {
			return D3D12_LOGIC_OP_EQUIV;
		}

		if (value == "AndReverse") {
			return D3D12_LOGIC_OP_AND_REVERSE;
		}

		if (value == "AndInverted") {
			return D3D12_LOGIC_OP_AND_INVERTED;
		}

		if (value == "OrReverse") {
			return D3D12_LOGIC_OP_OR_REVERSE;
		}

		if (value == "OrInverted") {
			return D3D12_LOGIC_OP_OR_INVERTED;
		}

		return D3D12_LOGIC_OP_NOOP;
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

	D3D12_DEPTH_WRITE_MASK ParseDepthWriteMask(const std::string& value) {
		if (value == "Zero") {
			return D3D12_DEPTH_WRITE_MASK_ZERO;
		}

		return D3D12_DEPTH_WRITE_MASK_ALL;
	}

	D3D12_STENCIL_OP ParseStencilOp(const std::string& value) {
		if (value == "Zero") {
			return D3D12_STENCIL_OP_ZERO;
		}

		if (value == "Replace") {
			return D3D12_STENCIL_OP_REPLACE;
		}

		if (value == "IncrSat") {
			return D3D12_STENCIL_OP_INCR_SAT;
		}

		if (value == "DecrSat") {
			return D3D12_STENCIL_OP_DECR_SAT;
		}

		if (value == "Invert") {
			return D3D12_STENCIL_OP_INVERT;
		}

		if (value == "Incr") {
			return D3D12_STENCIL_OP_INCR;
		}

		if (value == "Decr") {
			return D3D12_STENCIL_OP_DECR;
		}

		return D3D12_STENCIL_OP_KEEP;
	}

	D3D12_PRIMITIVE_TOPOLOGY_TYPE ParsePrimitiveTopologyType(const std::string& value) {
		if (value == "Point") {
			return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
		}

		if (value == "Line") {
			return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
		}

		if (value == "Triangle") {
			return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		}

		if (value == "Patch") {
			return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
		}

		return D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
	}

	D3D12_INDEX_BUFFER_STRIP_CUT_VALUE ParseStripCut(const std::string& value) {
		if (value == "0xFFFF") {
			return D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF;
		}

		if (value == "0xFFFFFFFF") {
			return D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
		}

		return D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
	}

	D3D12_PIPELINE_STATE_FLAGS ParsePipelineStateFlags(const rapidjson::Value& value) {
		D3D12_PIPELINE_STATE_FLAGS flags{ D3D12_PIPELINE_STATE_FLAG_NONE };
		if (value.IsArray() == false) {
			return flags;
		}

		for (const rapidjson::Value& entry : value.GetArray()) {
			if (entry.IsString() == false) {
				continue;
			}

			std::string text{ entry.GetString() };
			if (text == "ToolDebug") {
				flags = static_cast<D3D12_PIPELINE_STATE_FLAGS>(flags | D3D12_PIPELINE_STATE_FLAG_TOOL_DEBUG);
			}
		}

		return flags;
	}

	bool ParseShaderData(const rapidjson::Value& objectValue, const char* key, PipelineShaderData& shaderData) {
		if (objectValue.HasMember(key) == false || objectValue[key].IsObject() == false) {
			shaderData.Source = {};
			shaderData.Identifier = {};
			return true;
		}

		const rapidjson::Value& shaderObject{ objectValue[key] };
		shaderData.Source = ReadOptionalString(shaderObject, "Source", {});
		shaderData.Identifier = ReadOptionalString(shaderObject, "Identifier", {});
		if (shaderData.Source.empty() == true || shaderData.Identifier.empty() == true) {
			return false;
		}

		return true;
	}

	void InitializeDefaultPipelineDescription(PipelineDescriptionData& pipelineData) {
		pipelineData.Type = "Graphics";
		pipelineData.BlendState = CD3DX12_BLEND_DESC{ D3D12_DEFAULT };
		pipelineData.SampleMask = UINT_MAX;
		pipelineData.RasterizerState = CD3DX12_RASTERIZER_DESC{ D3D12_DEFAULT };
		pipelineData.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC{ D3D12_DEFAULT };
		pipelineData.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
		pipelineData.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pipelineData.RtvFormats.fill(DXGI_FORMAT_UNKNOWN);
		pipelineData.NumRenderTargets = 0;
		pipelineData.DsvFormat = DXGI_FORMAT_UNKNOWN;
		pipelineData.SampleDesc.Count = 1;
		pipelineData.SampleDesc.Quality = 0;
		pipelineData.NodeMask = 0;
		pipelineData.CachedPso.pCachedBlob = nullptr;
		pipelineData.CachedPso.CachedBlobSizeInBytes = 0;
		pipelineData.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	}

	bool ParsePipelineDescription(const rapidjson::Document& document, const std::filesystem::path& path, PipelineDescriptionData& pipelineData) {
		if (document.IsObject() == false) {
			return false;
		}

		InitializeDefaultPipelineDescription(pipelineData);
		pipelineData.Name = ReadOptionalString(document, "Name", path.stem().string());
		pipelineData.Type = ReadOptionalString(document, "Type", "Graphics");
		if (pipelineData.Type != "Graphics" && pipelineData.Type != "Compute") {
			return false;
		}

		pipelineData.RootSignatureName = ReadOptionalString(document, "RootSignature", {});
		if (pipelineData.RootSignatureName.empty() == true) {
			return false;
		}

		if (ParseShaderData(document, "VS", pipelineData.Vs) == false || ParseShaderData(document, "PS", pipelineData.Ps) == false || ParseShaderData(document, "DS", pipelineData.Ds) == false || ParseShaderData(document, "HS", pipelineData.Hs) == false || ParseShaderData(document, "GS", pipelineData.Gs) == false || ParseShaderData(document, "CS", pipelineData.Cs) == false) {
			return false;
		}

		if (document.HasMember("InputLayout") == true && document["InputLayout"].IsArray() == true) {
			for (const rapidjson::Value& inputElementValue : document["InputLayout"].GetArray()) {
				if (inputElementValue.IsObject() == false) {
					continue;
				}

				pipelineData.InputLayout.SemanticNames.emplace_back(ReadOptionalString(inputElementValue, "SemanticName", ""));
				D3D12_INPUT_ELEMENT_DESC element{};
				element.SemanticName = pipelineData.InputLayout.SemanticNames.back().c_str();
				element.SemanticIndex = ReadOptionalUint(inputElementValue, "SemanticIndex", 0);
				element.Format = ParseFormat(ReadOptionalString(inputElementValue, "Format", "UNKNOWN"));
				element.InputSlot = ReadOptionalUint(inputElementValue, "InputSlot", 0);
				element.AlignedByteOffset = ReadOptionalUint(inputElementValue, "AlignedByteOffset", D3D12_APPEND_ALIGNED_ELEMENT);
				element.InputSlotClass = ParseInputClassification(ReadOptionalString(inputElementValue, "InputSlotClass", "PerVertexData"));
				element.InstanceDataStepRate = ReadOptionalUint(inputElementValue, "InstanceDataStepRate", 0);
				pipelineData.InputLayout.Elements.emplace_back(element);
			}
		}

		if (document.HasMember("StreamOutput") == true && document["StreamOutput"].IsObject() == true) {
			const rapidjson::Value& streamOutput{ document["StreamOutput"] };
			if (streamOutput.HasMember("Entries") == true && streamOutput["Entries"].IsArray() == true) {
				for (const rapidjson::Value& entryValue : streamOutput["Entries"].GetArray()) {
					if (entryValue.IsObject() == false) {
						continue;
					}

					pipelineData.StreamOutput.SemanticNames.emplace_back(ReadOptionalString(entryValue, "SemanticName", ""));
					D3D12_SO_DECLARATION_ENTRY soEntry{};
					soEntry.Stream = ReadOptionalUint(entryValue, "Stream", 0);
					soEntry.SemanticName = pipelineData.StreamOutput.SemanticNames.back().c_str();
					soEntry.SemanticIndex = ReadOptionalUint(entryValue, "SemanticIndex", 0);
					soEntry.StartComponent = static_cast<BYTE>(ReadOptionalUint(entryValue, "StartComponent", 0));
					soEntry.ComponentCount = static_cast<BYTE>(ReadOptionalUint(entryValue, "ComponentCount", 4));
					soEntry.OutputSlot = ReadOptionalUint(entryValue, "OutputSlot", 0);
					pipelineData.StreamOutput.Entries.emplace_back(soEntry);
				}
			}

			if (streamOutput.HasMember("BufferStrides") == true && streamOutput["BufferStrides"].IsArray() == true) {
				for (const rapidjson::Value& strideValue : streamOutput["BufferStrides"].GetArray()) {
					if (strideValue.IsUint() == false) {
						continue;
					}

					pipelineData.StreamOutput.BufferStrides.emplace_back(strideValue.GetUint());
				}
			}
		}

		if (document.HasMember("BlendState") == true && document["BlendState"].IsObject() == true) {
			const rapidjson::Value& blendState{ document["BlendState"] };
			pipelineData.BlendState.AlphaToCoverageEnable = ReadOptionalBool(blendState, "AlphaToCoverageEnable", false);
			pipelineData.BlendState.IndependentBlendEnable = ReadOptionalBool(blendState, "IndependentBlendEnable", false);
			if (blendState.HasMember("RenderTargets") == true && blendState["RenderTargets"].IsArray() == true) {
				std::size_t index{ 0 };
				for (const rapidjson::Value& targetValue : blendState["RenderTargets"].GetArray()) {
					if (index >= std::size(pipelineData.BlendState.RenderTarget) || targetValue.IsObject() == false) {
						continue;
					}

					D3D12_RENDER_TARGET_BLEND_DESC& renderTargetDesc{ pipelineData.BlendState.RenderTarget[index] };
					renderTargetDesc.BlendEnable = ReadOptionalBool(targetValue, "BlendEnable", false);
					renderTargetDesc.LogicOpEnable = ReadOptionalBool(targetValue, "LogicOpEnable", false);
					renderTargetDesc.SrcBlend = ParseBlend(ReadOptionalString(targetValue, "SrcBlend", "One"));
					renderTargetDesc.DestBlend = ParseBlend(ReadOptionalString(targetValue, "DestBlend", "Zero"));
					renderTargetDesc.BlendOp = ParseBlendOp(ReadOptionalString(targetValue, "BlendOp", "Add"));
					renderTargetDesc.SrcBlendAlpha = ParseBlend(ReadOptionalString(targetValue, "SrcBlendAlpha", "One"));
					renderTargetDesc.DestBlendAlpha = ParseBlend(ReadOptionalString(targetValue, "DestBlendAlpha", "Zero"));
					renderTargetDesc.BlendOpAlpha = ParseBlendOp(ReadOptionalString(targetValue, "BlendOpAlpha", "Add"));
					renderTargetDesc.LogicOp = ParseLogicOp(ReadOptionalString(targetValue, "LogicOp", "Noop"));
					renderTargetDesc.RenderTargetWriteMask = static_cast<UINT8>(ReadOptionalUint(targetValue, "RenderTargetWriteMask", D3D12_COLOR_WRITE_ENABLE_ALL));
					index += 1;
				}
			}
		}

		pipelineData.SampleMask = ReadOptionalUint(document, "SampleMask", UINT_MAX);

		if (document.HasMember("RasterizerState") == true && document["RasterizerState"].IsObject() == true) {
			const rapidjson::Value& rasterizer{ document["RasterizerState"] };
			pipelineData.RasterizerState.FillMode = ParseFillMode(ReadOptionalString(rasterizer, "FillMode", "Solid"));
			pipelineData.RasterizerState.CullMode = ParseCullMode(ReadOptionalString(rasterizer, "CullMode", "Back"));
			pipelineData.RasterizerState.FrontCounterClockwise = ReadOptionalBool(rasterizer, "FrontCounterClockwise", false);
			pipelineData.RasterizerState.DepthBias = ReadOptionalInt(rasterizer, "DepthBias", D3D12_DEFAULT_DEPTH_BIAS);
			pipelineData.RasterizerState.DepthBiasClamp = ReadOptionalFloat(rasterizer, "DepthBiasClamp", D3D12_DEFAULT_DEPTH_BIAS_CLAMP);
			pipelineData.RasterizerState.SlopeScaledDepthBias = ReadOptionalFloat(rasterizer, "SlopeScaledDepthBias", D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS);
			pipelineData.RasterizerState.DepthClipEnable = ReadOptionalBool(rasterizer, "DepthClipEnable", true);
			pipelineData.RasterizerState.MultisampleEnable = ReadOptionalBool(rasterizer, "MultisampleEnable", false);
			pipelineData.RasterizerState.AntialiasedLineEnable = ReadOptionalBool(rasterizer, "AntialiasedLineEnable", false);
			pipelineData.RasterizerState.ForcedSampleCount = ReadOptionalUint(rasterizer, "ForcedSampleCount", 0);
			pipelineData.RasterizerState.ConservativeRaster = ParseConservativeRaster(ReadOptionalString(rasterizer, "ConservativeRaster", "Off"));
		}

		if (document.HasMember("DepthStencilState") == true && document["DepthStencilState"].IsObject() == true) {
			const rapidjson::Value& depthStencil{ document["DepthStencilState"] };
			pipelineData.DepthStencilState.DepthEnable = ReadOptionalBool(depthStencil, "DepthEnable", true);
			pipelineData.DepthStencilState.DepthWriteMask = ParseDepthWriteMask(ReadOptionalString(depthStencil, "DepthWriteMask", "All"));
			pipelineData.DepthStencilState.DepthFunc = ParseComparisonFunc(ReadOptionalString(depthStencil, "DepthFunc", "Less"));
			pipelineData.DepthStencilState.StencilEnable = ReadOptionalBool(depthStencil, "StencilEnable", false);
			pipelineData.DepthStencilState.StencilReadMask = static_cast<UINT8>(ReadOptionalUint(depthStencil, "StencilReadMask", D3D12_DEFAULT_STENCIL_READ_MASK));
			pipelineData.DepthStencilState.StencilWriteMask = static_cast<UINT8>(ReadOptionalUint(depthStencil, "StencilWriteMask", D3D12_DEFAULT_STENCIL_WRITE_MASK));

			if (depthStencil.HasMember("FrontFace") == true && depthStencil["FrontFace"].IsObject() == true) {
				const rapidjson::Value& frontFace{ depthStencil["FrontFace"] };
				pipelineData.DepthStencilState.FrontFace.StencilFailOp = ParseStencilOp(ReadOptionalString(frontFace, "StencilFailOp", "Keep"));
				pipelineData.DepthStencilState.FrontFace.StencilDepthFailOp = ParseStencilOp(ReadOptionalString(frontFace, "StencilDepthFailOp", "Keep"));
				pipelineData.DepthStencilState.FrontFace.StencilPassOp = ParseStencilOp(ReadOptionalString(frontFace, "StencilPassOp", "Keep"));
				pipelineData.DepthStencilState.FrontFace.StencilFunc = ParseComparisonFunc(ReadOptionalString(frontFace, "StencilFunc", "Always"));
			}

			if (depthStencil.HasMember("BackFace") == true && depthStencil["BackFace"].IsObject() == true) {
				const rapidjson::Value& backFace{ depthStencil["BackFace"] };
				pipelineData.DepthStencilState.BackFace.StencilFailOp = ParseStencilOp(ReadOptionalString(backFace, "StencilFailOp", "Keep"));
				pipelineData.DepthStencilState.BackFace.StencilDepthFailOp = ParseStencilOp(ReadOptionalString(backFace, "StencilDepthFailOp", "Keep"));
				pipelineData.DepthStencilState.BackFace.StencilPassOp = ParseStencilOp(ReadOptionalString(backFace, "StencilPassOp", "Keep"));
				pipelineData.DepthStencilState.BackFace.StencilFunc = ParseComparisonFunc(ReadOptionalString(backFace, "StencilFunc", "Always"));
			}
		}

		pipelineData.IBStripCutValue = ParseStripCut(ReadOptionalString(document, "IBStripCutValue", "Disabled"));
		pipelineData.PrimitiveTopologyType = ParsePrimitiveTopologyType(ReadOptionalString(document, "PrimitiveTopologyType", "Triangle"));

		if (document.HasMember("RTVFormats") == true && document["RTVFormats"].IsArray() == true) {
			std::size_t index{ 0 };
			for (const rapidjson::Value& rtvValue : document["RTVFormats"].GetArray()) {
				if (index >= pipelineData.RtvFormats.size() || rtvValue.IsString() == false) {
					continue;
				}

				pipelineData.RtvFormats[index] = ParseFormat(rtvValue.GetString());
				index += 1;
			}

			pipelineData.NumRenderTargets = static_cast<UINT>(index);
		}

		pipelineData.DsvFormat = ParseFormat(ReadOptionalString(document, "DSVFormat", "UNKNOWN"));
		if (document.HasMember("SampleDesc") == true && document["SampleDesc"].IsObject() == true) {
			const rapidjson::Value& sampleDesc{ document["SampleDesc"] };
			pipelineData.SampleDesc.Count = ReadOptionalUint(sampleDesc, "Count", 1);
			pipelineData.SampleDesc.Quality = ReadOptionalUint(sampleDesc, "Quality", 0);
		}

		pipelineData.NodeMask = ReadOptionalUint(document, "NodeMask", 0);
		pipelineData.Flags = document.HasMember("Flags") == true ? ParsePipelineStateFlags(document["Flags"]) : D3D12_PIPELINE_STATE_FLAG_NONE;
		return true;
	}

	bool ResolveShaderByteCode(const PipelineShaderData& shaderData, Game::Base::Shader& shader, D3D12_SHADER_BYTECODE& shaderByteCode) {
		shaderByteCode = D3D12_SHADER_BYTECODE{};

		if (shaderData.Source.empty() == true || shaderData.Identifier.empty() == true) {
			return false;
		}

		shader.Initialize(shaderData.Source);

		if (shader.HasByteCode(shaderData.Identifier) == false) {
			return false;
		}

		shaderByteCode = shader.GetByteCode(shaderData.Identifier);
		return shaderByteCode.pShaderBytecode != nullptr && shaderByteCode.BytecodeLength > 0;
	}

	bool CreatePipelineFromFile(ID3D12Device* device, const std::filesystem::path& path, std::unordered_map<std::string, CompiledPipelineData>& pipelines) {
		std::ifstream input{ path, std::ios::binary };
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
			std::string errorMessage{ rapidjson::GetParseError_En(document.GetParseError()) };
			std::string pathString{ path.string() };
			std::string parseCode{ std::to_string(document.GetParseError()) };
			std::string parseOffset{ std::to_string(document.GetErrorOffset()) };
			ErrorHandler::report("Failed to parse pipeline description file. Path: " + pathString, "Code: " + parseCode + ", Offset: " + parseOffset + ", Message: " + errorMessage, ErrorHandler::Level::Critical); 
			return false;
		}

		PipelineDescriptionData pipelineData{};
		if (ParsePipelineDescription(document, path, pipelineData) == false) {
			ErrorHandler::report("Failed to parse pipeline description data.", "Path: " + path.string(), ErrorHandler::Level::Critical);
			return false;
		}

		if (Game::Base::RootSignature::HasCompiledRootSignature(pipelineData.RootSignatureName) == false) {
			ErrorHandler::report("Root signature specified in pipeline description does not exist.", "Pipeline: " + pipelineData.Name + ", Root Signature: " + pipelineData.RootSignatureName, ErrorHandler::Level::Critical);
			return false;
		}

		Game::Base::RootSignature rootSignature{};
		if (rootSignature.Initialize(pipelineData.RootSignatureName) == false) {
			return false;
		}

		ComPtr<ID3D12PipelineState> pipelineState{ nullptr };
		Game::Base::Shader vertexShader{};
		Game::Base::Shader pixelShader{};
		Game::Base::Shader domainShader{};
		Game::Base::Shader hullShader{};
		Game::Base::Shader geometryShader{};
		Game::Base::Shader computeShader{};

		D3D12_SHADER_BYTECODE vertexShaderByteCode{};
		D3D12_SHADER_BYTECODE pixelShaderByteCode{};
		D3D12_SHADER_BYTECODE domainShaderByteCode{};
		D3D12_SHADER_BYTECODE hullShaderByteCode{};
		D3D12_SHADER_BYTECODE geometryShaderByteCode{};
		D3D12_SHADER_BYTECODE computeShaderByteCode{};

		if (pipelineData.Type == "Compute") {
			if (ResolveShaderByteCode(pipelineData.Cs, computeShader, computeShaderByteCode) == false) {
				ErrorHandler::report("Failed to resolve compute shader byte code.", "Pipeline: " + pipelineData.Name, ErrorHandler::Level::Critical);
				return false;
			}

			D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc{};
			computeDesc.pRootSignature = rootSignature.Get();
			computeDesc.CS = computeShaderByteCode;
			computeDesc.NodeMask = pipelineData.NodeMask;
			computeDesc.CachedPSO = pipelineData.CachedPso;
			computeDesc.Flags = pipelineData.Flags;

			HRESULT result{ device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(pipelineState.GetAddressOf())) };
			if (FAILED(result) == true || pipelineState == nullptr) {
				return false;
			}

		} else {
			if (ResolveShaderByteCode(pipelineData.Vs, vertexShader, vertexShaderByteCode) == false || ResolveShaderByteCode(pipelineData.Ps, pixelShader, pixelShaderByteCode) == false) {
				ErrorHandler::report("Failed to resolve required graphics shader byte code.", "Pipeline: " + pipelineData.Name, ErrorHandler::Level::Critical);
				return false;
			}

			if (pipelineData.Ds.Source.empty() == false || pipelineData.Ds.Identifier.empty() == false) {
				if (ResolveShaderByteCode(pipelineData.Ds, domainShader, domainShaderByteCode) == false) {
					ErrorHandler::report("Failed to resolve domain shader byte code.", "Pipeline: " + pipelineData.Name, ErrorHandler::Level::Critical);
					return false;
				}
			}

			if (pipelineData.Hs.Source.empty() == false || pipelineData.Hs.Identifier.empty() == false) {
				if (ResolveShaderByteCode(pipelineData.Hs, hullShader, hullShaderByteCode) == false) {
					ErrorHandler::report("Failed to resolve hull shader byte code.", "Pipeline: " + pipelineData.Name, ErrorHandler::Level::Critical);
					return false;
				}
			}

			if (pipelineData.Gs.Source.empty() == false || pipelineData.Gs.Identifier.empty() == false) {
				if (ResolveShaderByteCode(pipelineData.Gs, geometryShader, geometryShaderByteCode) == false) {
					ErrorHandler::report("Failed to resolve geometry shader byte code.", "Pipeline: " + pipelineData.Name, ErrorHandler::Level::Critical);
					return false;
				}
			}

			D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsDesc{};
			graphicsDesc.pRootSignature = rootSignature.Get();
			graphicsDesc.VS = vertexShaderByteCode;
			graphicsDesc.PS = pixelShaderByteCode;
			graphicsDesc.DS = domainShaderByteCode;
			graphicsDesc.HS = hullShaderByteCode;
			graphicsDesc.GS = geometryShaderByteCode;
			graphicsDesc.BlendState = pipelineData.BlendState;
			graphicsDesc.SampleMask = pipelineData.SampleMask;
			graphicsDesc.RasterizerState = pipelineData.RasterizerState;
			graphicsDesc.DepthStencilState = pipelineData.DepthStencilState;
			graphicsDesc.InputLayout.pInputElementDescs = pipelineData.InputLayout.Elements.empty() == true ? nullptr : pipelineData.InputLayout.Elements.data();
			graphicsDesc.InputLayout.NumElements = static_cast<UINT>(pipelineData.InputLayout.Elements.size());
			graphicsDesc.IBStripCutValue = pipelineData.IBStripCutValue;
			graphicsDesc.PrimitiveTopologyType = pipelineData.PrimitiveTopologyType;
			graphicsDesc.NumRenderTargets = pipelineData.NumRenderTargets;

			for (std::size_t index{ 0 }; index < pipelineData.RtvFormats.size(); index += 1) {
				graphicsDesc.RTVFormats[index] = pipelineData.RtvFormats[index];
			}

			graphicsDesc.DSVFormat = pipelineData.DsvFormat;
			graphicsDesc.SampleDesc = pipelineData.SampleDesc;
			graphicsDesc.NodeMask = pipelineData.NodeMask;
			graphicsDesc.CachedPSO = pipelineData.CachedPso;
			graphicsDesc.Flags = pipelineData.Flags;
			graphicsDesc.StreamOutput.pSODeclaration = pipelineData.StreamOutput.Entries.empty() == true ? nullptr : pipelineData.StreamOutput.Entries.data();
			graphicsDesc.StreamOutput.NumEntries = static_cast<UINT>(pipelineData.StreamOutput.Entries.size());
			graphicsDesc.StreamOutput.pBufferStrides = pipelineData.StreamOutput.BufferStrides.empty() == true ? nullptr : pipelineData.StreamOutput.BufferStrides.data();
			graphicsDesc.StreamOutput.NumStrides = static_cast<UINT>(pipelineData.StreamOutput.BufferStrides.size());
			graphicsDesc.StreamOutput.RasterizedStream = 0;

			HRESULT result{ device->CreateGraphicsPipelineState(&graphicsDesc, IID_PPV_ARGS(pipelineState.GetAddressOf())) };
			if (FAILED(result) == true || pipelineState == nullptr) {
				return false;
			}
		}

		if (pipelines.contains(pipelineData.Name) == true) {
			ErrorHandler::report("Duplicated pipeline name detected.", "Pipeline: " + pipelineData.Name + ", Path: " + path.string(), ErrorHandler::Level::Critical);
			return false;
		}

		CompiledPipelineData compiledPipelineData{};
		compiledPipelineData.RootSignature = rootSignature.Get();
		compiledPipelineData.PipelineState = pipelineState;
		pipelines[pipelineData.Name] = compiledPipelineData;
		return true;
	}
}

namespace Game {
	namespace Base {
		bool Pipeline::Initialize(const std::string& pipelineName) {
			if (pipelineName.empty() == true) {
				return false;
			}

			if (HasCompiledPipeline(pipelineName) == false) {
				return false;
			}

			std::scoped_lock<std::mutex> lock{ GetCompiledPipelinesMutex() };

			const std::unordered_map<std::string, CompiledPipelineData>& pipelines{ GetCompiledPipelinesStorage() };
			auto iterator{ pipelines.find(pipelineName) };
			if (iterator == pipelines.end()) {
				return false;
			}

			mRootSignature = iterator->second.RootSignature;
			mPipelineState = iterator->second.PipelineState;
			return true;
		}

		Pipeline* Pipeline::Set(Pipeline* pipeline, ID3D12GraphicsCommandList* commandList) {
			if (commandList == nullptr) {
				return this;
			}

			if (pipeline == nullptr || pipeline->GetRootSignature() != GetRootSignature()) {
				commandList->SetGraphicsRootSignature(mRootSignature.Get());
			}

			if (pipeline == nullptr || pipeline->Get() != Get()) {
				commandList->SetPipelineState(mPipelineState.Get());
			}

			return this;
		}

		ID3D12PipelineState* Pipeline::Get() const {
			return mPipelineState.Get();
		}

		ID3D12RootSignature* Pipeline::GetRootSignature() const {
			return mRootSignature.Get();
		}

		bool Pipeline::operator==(const Pipeline& other) const {
			return GetRootSignature() == other.GetRootSignature() && Get() == other.Get();
		}

		bool Pipeline::operator!=(const Pipeline& other) const {
			return (*this == other) == false;
		}

		bool Pipeline::operator<(const Pipeline& other) const {
			std::less<ID3D12RootSignature*> rootSignatureComparator{};
			if (rootSignatureComparator(GetRootSignature(), other.GetRootSignature()) == true) {
				return true;
			}

			if (rootSignatureComparator(other.GetRootSignature(), GetRootSignature()) == true) {
				return false;
			}

			std::less<ID3D12PipelineState*> pipelineStateComparator{};
			return pipelineStateComparator(Get(), other.Get());
		}

		bool Pipeline::HasCompiledPipeline(const std::string& pipelineName) {
			std::scoped_lock<std::mutex> lock{ GetCompiledPipelinesMutex() };
			const std::unordered_map<std::string, CompiledPipelineData>& pipelines{ GetCompiledPipelinesStorage() };
			return pipelines.contains(pipelineName);
		}

		bool PreCompilePipelines(ID3D12Device* device) {
			if (device == nullptr) {
				return false;
			}

			std::filesystem::path folderPath{ std::filesystem::current_path() / "Shader" / "PSO" };
			if (std::filesystem::exists(folderPath) == false) {
				return true;
			}

			std::unordered_map<std::string, CompiledPipelineData> pipelines{};
			for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folderPath)) {
				if (entry.is_regular_file() == false || entry.path().extension() != ".json") {
					continue;
				}

				std::string stem{ entry.path().stem().string() };
				if (stem.rfind("Schema", 0) == 0) {
					continue;
				}

				if (CreatePipelineFromFile(device, entry.path(), pipelines) == false) {
					return false;
				}
			}

			std::scoped_lock<std::mutex> lock{ GetCompiledPipelinesMutex() };
			GetCompiledPipelinesStorage() = std::move(pipelines);
			return true;
		}

	}
}
