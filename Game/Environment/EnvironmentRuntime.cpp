#include "Game/Environment/EnvironmentRuntime.h"

#include <bit>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

#include "Core/DX/DesciptorHeap.h"
#include "Utility/DirectXInclude.h"
#include "Utility/ErrorHandler.h"

namespace Game {
    namespace {
        constexpr std::uint32_t EnvironmentGpuRootConstantDwordCount{ 12u };
        constexpr std::uint32_t EnvironmentGpuStatusDwordCount{ 16u };
        constexpr std::uint32_t EnvironmentComputeThreadGroupSize{ 64u };
        constexpr std::uint32_t InvalidDescriptorIndex{ 0xffffffffu };

        struct EnvironmentGpuRootConstants final {
        public:
            std::uint32_t mStatusUavIndex{};
            std::uint32_t mFrameIndexLow{};
            std::uint32_t mFrameIndexHigh{};
            std::uint32_t mTerrainHeightSrvIndex{};
            std::uint32_t mTerrainSplatSrvIndex{};
            std::uint32_t mTerrainWidth{};
            std::uint32_t mTerrainHeight{};
            std::uint32_t mFocusPositionX{};
            std::uint32_t mFocusPositionY{};
            std::uint32_t mFocusPositionZ{};
            std::uint32_t mDispatchThreadGroupSize{};
            std::uint32_t mReserved0{};
        };

        static_assert(sizeof(EnvironmentGpuRootConstants) == sizeof(std::uint32_t) * EnvironmentGpuRootConstantDwordCount);

        std::string ReadBinaryString(std::ifstream& Input) {
            std::uint32_t Length{};
            Input.read(reinterpret_cast<char*>(&Length), sizeof(std::uint32_t));
            std::string Value(Length, '\0');
            if (Length > 0u) {
                Input.read(Value.data(), Length);
            }

            return Value;
        }

        std::filesystem::path BuildShaderBinaryPath(const std::filesystem::path& SourceFileName) {
            std::filesystem::path BinaryFileName{ SourceFileName.stem().wstring() + L".shaderbin" };
            return std::filesystem::current_path() / "Shader" / "Binarys" / BinaryFileName;
        }

        std::vector<std::uint8_t> LoadShaderByteCode(const std::filesystem::path& SourceFileName, const std::string& Identifier) {
            std::ifstream Input{ BuildShaderBinaryPath(SourceFileName), std::ios::binary };
            if (Input.is_open() == false) {
                return {};
            }

            std::uint32_t Magic{};
            std::uint32_t Version{};
            std::uint32_t Count{};
            Input.read(reinterpret_cast<char*>(&Magic), sizeof(std::uint32_t));
            Input.read(reinterpret_cast<char*>(&Version), sizeof(std::uint32_t));
            Input.read(reinterpret_cast<char*>(&Count), sizeof(std::uint32_t));
            if (Magic != 0x30444853u || Version != 1u) {
                return {};
            }

            for (std::uint32_t Index{ 0u }; Index < Count; ++Index) {
                std::string CurrentIdentifier{ ReadBinaryString(Input) };
                std::uint64_t ByteCodeSize{};
                Input.read(reinterpret_cast<char*>(&ByteCodeSize), sizeof(std::uint64_t));
                std::vector<std::uint8_t> ByteCode(static_cast<std::size_t>(ByteCodeSize));
                if (ByteCodeSize > 0u) {
                    Input.read(reinterpret_cast<char*>(ByteCode.data()), static_cast<std::streamsize>(ByteCodeSize));
                }

                if (CurrentIdentifier == Identifier) {
                    return ByteCode;
                }
            }

            return {};
        }

        EnvironmentGpuRootConstants BuildEnvironmentGpuRootConstants(const EnvironmentFrameInput& Input, std::uint32_t StatusUavIndex) {
            EnvironmentGpuRootConstants Constants{};
            Constants.mStatusUavIndex = StatusUavIndex;
            Constants.mFrameIndexLow = static_cast<std::uint32_t>(Input.mFrameIndex & 0xffffffffULL);
            Constants.mFrameIndexHigh = static_cast<std::uint32_t>((Input.mFrameIndex >> 32ULL) & 0xffffffffULL);
            Constants.mTerrainHeightSrvIndex = Input.mTerrain.mHeightSrvIndex;
            Constants.mTerrainSplatSrvIndex = Input.mTerrain.mSplatSrvIndex;
            Constants.mTerrainWidth = Input.mTerrain.mWidth;
            Constants.mTerrainHeight = Input.mTerrain.mHeight;
            Constants.mFocusPositionX = std::bit_cast<std::uint32_t>(Input.mFocusPosition.x);
            Constants.mFocusPositionY = std::bit_cast<std::uint32_t>(Input.mFocusPosition.y);
            Constants.mFocusPositionZ = std::bit_cast<std::uint32_t>(Input.mFocusPosition.z);
            Constants.mDispatchThreadGroupSize = EnvironmentComputeThreadGroupSize;
            Constants.mReserved0 = 0u;
            return Constants;
        }
    }

    EnvironmentPhysicsAdapter::EnvironmentPhysicsAdapter() {
    }

    EnvironmentPhysicsAdapter::~EnvironmentPhysicsAdapter() {
    }

    EnvironmentRuntime::EnvironmentRuntime()
        : mDevice{},
        mAllocator{},
        mSrvHeap{},
        mCopyQueue{},
        mComputeQueue{},
        mPhysicsAdapter{},
        mRenderContext{},
        mComputeRootSignature{},
        mPreparePipelineState{},
        mGpuStatusBuffer{},
        mLastGpuDispatchFuture{},
        mConfigPath{},
        mGpuStatusUavIndex{ InvalidDescriptorIndex },
        mInitialized{},
        mGpuDrivenEnabled{},
        mGpuResourcesInitialized{} {
    }

    EnvironmentRuntime::~EnvironmentRuntime() {
    }

    EnvironmentRuntime::EnvironmentRuntime(EnvironmentRuntime&& Other) noexcept
        : mDevice{ Other.mDevice },
        mAllocator{ Other.mAllocator },
        mSrvHeap{ Other.mSrvHeap },
        mCopyQueue{ Other.mCopyQueue },
        mComputeQueue{ Other.mComputeQueue },
        mPhysicsAdapter{ Other.mPhysicsAdapter },
        mRenderContext{ std::move(Other.mRenderContext) },
        mComputeRootSignature{ std::move(Other.mComputeRootSignature) },
        mPreparePipelineState{ std::move(Other.mPreparePipelineState) },
        mGpuStatusBuffer{ std::move(Other.mGpuStatusBuffer) },
        mLastGpuDispatchFuture{ std::move(Other.mLastGpuDispatchFuture) },
        mConfigPath{ std::move(Other.mConfigPath) },
        mGpuStatusUavIndex{ Other.mGpuStatusUavIndex },
        mInitialized{ Other.mInitialized },
        mGpuDrivenEnabled{ Other.mGpuDrivenEnabled },
        mGpuResourcesInitialized{ Other.mGpuResourcesInitialized } {
        Other.mDevice = nullptr;
        Other.mAllocator = nullptr;
        Other.mSrvHeap = nullptr;
        Other.mCopyQueue = nullptr;
        Other.mComputeQueue = nullptr;
        Other.mPhysicsAdapter = nullptr;
        Other.mGpuStatusUavIndex = InvalidDescriptorIndex;
        Other.mInitialized = false;
        Other.mGpuDrivenEnabled = false;
        Other.mGpuResourcesInitialized = false;
    }

    EnvironmentRuntime& EnvironmentRuntime::operator=(EnvironmentRuntime&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mDevice = Other.mDevice;
        mAllocator = Other.mAllocator;
        mSrvHeap = Other.mSrvHeap;
        mCopyQueue = Other.mCopyQueue;
        mComputeQueue = Other.mComputeQueue;
        mPhysicsAdapter = Other.mPhysicsAdapter;
        mRenderContext = std::move(Other.mRenderContext);
        mComputeRootSignature = std::move(Other.mComputeRootSignature);
        mPreparePipelineState = std::move(Other.mPreparePipelineState);
        mGpuStatusBuffer = std::move(Other.mGpuStatusBuffer);
        mLastGpuDispatchFuture = std::move(Other.mLastGpuDispatchFuture);
        mConfigPath = std::move(Other.mConfigPath);
        mGpuStatusUavIndex = Other.mGpuStatusUavIndex;
        mInitialized = Other.mInitialized;
        mGpuDrivenEnabled = Other.mGpuDrivenEnabled;
        mGpuResourcesInitialized = Other.mGpuResourcesInitialized;
        Other.mDevice = nullptr;
        Other.mAllocator = nullptr;
        Other.mSrvHeap = nullptr;
        Other.mCopyQueue = nullptr;
        Other.mComputeQueue = nullptr;
        Other.mPhysicsAdapter = nullptr;
        Other.mGpuStatusUavIndex = InvalidDescriptorIndex;
        Other.mInitialized = false;
        Other.mGpuDrivenEnabled = false;
        Other.mGpuResourcesInitialized = false;
        return *this;
    }

    bool EnvironmentRuntime::Initialize(const EnvironmentRuntimeDesc& Desc) {
        ResetGpuResources();
        mDevice = Desc.mDevice;
        mAllocator = Desc.mAllocator;
        mSrvHeap = Desc.mSrvHeap;
        mCopyQueue = Desc.mCopyQueue;
        mComputeQueue = Desc.mComputeQueue;
        mPhysicsAdapter = Desc.mPhysicsAdapter;
        mInitialized = Desc.mDevice != nullptr && Desc.mAllocator != nullptr && Desc.mSrvHeap != nullptr && Desc.mCopyQueue != nullptr;
        mGpuDrivenEnabled = false;
        if (mInitialized == true && Desc.mGpuDrivenEnabled == true && Desc.mComputeQueue != nullptr) {
            mGpuDrivenEnabled = InitializeGpuResources();
        }

        return mInitialized;
    }

    void EnvironmentRuntime::Reset() {
        ResetGpuResources();
        mDevice = nullptr;
        mAllocator = nullptr;
        mSrvHeap = nullptr;
        mCopyQueue = nullptr;
        mComputeQueue = nullptr;
        mPhysicsAdapter = nullptr;
        mRenderContext.Clear();
        mLastGpuDispatchFuture = RenderContract::Future{};
        mInitialized = false;
        mGpuDrivenEnabled = false;
    }

    void EnvironmentRuntime::SetConfigPath(const std::string& ConfigPath) {
        mConfigPath = ConfigPath;
    }

    const std::string& EnvironmentRuntime::GetConfigPath() const {
        return mConfigPath;
    }

    void EnvironmentRuntime::TickCpu(const EnvironmentFrameInput& Input) {
        static_cast<void>(Input);
    }

    RenderContract::Future EnvironmentRuntime::DispatchGpu(const EnvironmentFrameInput& Input) {
        if (mInitialized == false || mGpuDrivenEnabled == false || mGpuResourcesInitialized == false || mComputeQueue == nullptr || mSrvHeap == nullptr || mGpuStatusUavIndex == InvalidDescriptorIndex) {
            mLastGpuDispatchFuture = RenderContract::Future{};
            return mLastGpuDispatchFuture;
        }

        const EnvironmentGpuRootConstants RootConstants{ BuildEnvironmentGpuRootConstants(Input, mGpuStatusUavIndex) };

        Interface::ComputeQueueDispatchRequest DispatchRequest{};
        DispatchRequest.RootSignature = mComputeRootSignature;
        DispatchRequest.PipelineState = mPreparePipelineState;
        DispatchRequest.DescriptorHeaps = std::vector<ID3D12DescriptorHeap*>{ mSrvHeap->GetHeap() };
        DispatchRequest.RecordCommands = [RootConstants](ID3D12GraphicsCommandList* CommandList) {
            if (CommandList == nullptr) {
                return;
            }

            CommandList->SetComputeRoot32BitConstants(0, EnvironmentGpuRootConstantDwordCount, &RootConstants, 0);
        };
        DispatchRequest.ThreadGroupCountX = 1u;
        DispatchRequest.ThreadGroupCountY = 1u;
        DispatchRequest.ThreadGroupCountZ = 1u;

        mLastGpuDispatchFuture = mComputeQueue->EnqueueComputeFuture(DispatchRequest);
        mComputeQueue->DispatchComputes();
        return mLastGpuDispatchFuture;
    }

    void EnvironmentRuntime::Draw(ID3D12GraphicsCommandList* CommandList) {
        static_cast<void>(CommandList);
    }

    EnvironmentObjectRenderContext& EnvironmentRuntime::GetRenderContext() {
        return mRenderContext;
    }

    const EnvironmentObjectRenderContext& EnvironmentRuntime::GetRenderContext() const {
        return mRenderContext;
    }

    bool EnvironmentRuntime::IsInitialized() const {
        return mInitialized;
    }

    bool EnvironmentRuntime::IsGpuDrivenEnabled() const {
        return mGpuDrivenEnabled;
    }

    bool EnvironmentRuntime::InitializeGpuResources() {
        if (CreateComputeRootSignature() == false) {
            return false;
        }

        if (CreateComputePipelineState() == false) {
            return false;
        }

        if (CreateGpuStatusBuffer() == false) {
            return false;
        }

        mGpuResourcesInitialized = true;
        return true;
    }

    bool EnvironmentRuntime::CreateComputeRootSignature() {
        if (mDevice == nullptr) {
            return false;
        }

        D3D12_ROOT_PARAMETER1 RootParameter{};
        RootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        RootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        RootParameter.Constants.ShaderRegister = 1u;
        RootParameter.Constants.RegisterSpace = 0u;
        RootParameter.Constants.Num32BitValues = EnvironmentGpuRootConstantDwordCount;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC RootSignatureDesc{};
        RootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        RootSignatureDesc.Desc_1_1.NumParameters = 1u;
        RootSignatureDesc.Desc_1_1.pParameters = &RootParameter;
        RootSignatureDesc.Desc_1_1.NumStaticSamplers = 0u;
        RootSignatureDesc.Desc_1_1.pStaticSamplers = nullptr;
        RootSignatureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

        Microsoft::WRL::ComPtr<ID3DBlob> SerializedRootSignature{};
        Microsoft::WRL::ComPtr<ID3DBlob> ErrorBlob{};
        HRESULT SerializeResult{ D3D12SerializeVersionedRootSignature(&RootSignatureDesc, SerializedRootSignature.GetAddressOf(), ErrorBlob.GetAddressOf()) };
        if (FAILED(SerializeResult) == true || SerializedRootSignature == nullptr) {
            ErrorHandler::report("EnvironmentRuntime", "Failed to serialize environment compute root signature.", ErrorHandler::Level::Warning);
            return false;
        }

        HRESULT CreateResult{ mDevice->CreateRootSignature(0u, SerializedRootSignature->GetBufferPointer(), SerializedRootSignature->GetBufferSize(), IID_PPV_ARGS(mComputeRootSignature.GetAddressOf())) };
        if (FAILED(CreateResult) == true || mComputeRootSignature == nullptr) {
            ErrorHandler::report("EnvironmentRuntime", "Failed to create environment compute root signature.", ErrorHandler::Level::Warning);
            return false;
        }

        return true;
    }

    bool EnvironmentRuntime::CreateComputePipelineState() {
        if (mDevice == nullptr || mComputeRootSignature == nullptr) {
            return false;
        }

        std::vector<std::uint8_t> ShaderByteCode{ LoadShaderByteCode("EnvironmentObjectPrepareShader.hlsl", "cs_6_6:CsMain") };
        if (ShaderByteCode.empty() == true) {
            ErrorHandler::report("EnvironmentRuntime", "Failed to load EnvironmentObjectPrepareShader byte code.", ErrorHandler::Level::Warning);
            return false;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC PipelineDesc{};
        PipelineDesc.pRootSignature = mComputeRootSignature.Get();
        PipelineDesc.CS.pShaderBytecode = ShaderByteCode.data();
        PipelineDesc.CS.BytecodeLength = ShaderByteCode.size();
        PipelineDesc.NodeMask = 0u;
        PipelineDesc.CachedPSO = D3D12_CACHED_PIPELINE_STATE{};
        PipelineDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

        HRESULT CreateResult{ mDevice->CreateComputePipelineState(&PipelineDesc, IID_PPV_ARGS(mPreparePipelineState.GetAddressOf())) };
        if (FAILED(CreateResult) == true || mPreparePipelineState == nullptr) {
            ErrorHandler::report("EnvironmentRuntime", "Failed to create environment prepare compute pipeline.", ErrorHandler::Level::Warning);
            return false;
        }

        return true;
    }

    bool EnvironmentRuntime::CreateGpuStatusBuffer() {
        if (mDevice == nullptr || mSrvHeap == nullptr) {
            return false;
        }

        D3D12_HEAP_PROPERTIES HeapProperties{};
        HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
        HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        HeapProperties.CreationNodeMask = 1u;
        HeapProperties.VisibleNodeMask = 1u;

        D3D12_RESOURCE_DESC ResourceDesc{};
        ResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        ResourceDesc.Alignment = 0u;
        ResourceDesc.Width = sizeof(std::uint32_t) * EnvironmentGpuStatusDwordCount;
        ResourceDesc.Height = 1u;
        ResourceDesc.DepthOrArraySize = 1u;
        ResourceDesc.MipLevels = 1u;
        ResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        ResourceDesc.SampleDesc.Count = 1u;
        ResourceDesc.SampleDesc.Quality = 0u;
        ResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        HRESULT CreateResult{ mDevice->CreateCommittedResource(&HeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(mGpuStatusBuffer.GetAddressOf())) };
        if (FAILED(CreateResult) == true || mGpuStatusBuffer == nullptr) {
            ErrorHandler::report("EnvironmentRuntime", "Failed to create environment GPU status buffer.", ErrorHandler::Level::Warning);
            return false;
        }

        mGpuStatusBuffer->SetName(L"EnvironmentRuntime.GpuStatusBuffer");

        Core::DX::DescriptorHandle StatusUavHandle{ mSrvHeap->Allocate() };
        if (StatusUavHandle.IsValid() == false) {
            ErrorHandler::report("EnvironmentRuntime", "Failed to allocate environment GPU status UAV.", ErrorHandler::Level::Warning);
            return false;
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC UavDesc{};
        UavDesc.Format = DXGI_FORMAT_UNKNOWN;
        UavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        UavDesc.Buffer.FirstElement = 0u;
        UavDesc.Buffer.NumElements = EnvironmentGpuStatusDwordCount;
        UavDesc.Buffer.StructureByteStride = sizeof(std::uint32_t);
        UavDesc.Buffer.CounterOffsetInBytes = 0u;
        UavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

        mDevice->CreateUnorderedAccessView(mGpuStatusBuffer.Get(), nullptr, &UavDesc, StatusUavHandle.GetCPU());
        mGpuStatusUavIndex = StatusUavHandle.GetIndex();
        return true;
    }

    void EnvironmentRuntime::ResetGpuResources() {
        mComputeRootSignature.Reset();
        mPreparePipelineState.Reset();
        mGpuStatusBuffer.Reset();
        mGpuStatusUavIndex = InvalidDescriptorIndex;
        mLastGpuDispatchFuture = RenderContract::Future{};
        mGpuDrivenEnabled = false;
        mGpuResourcesInitialized = false;
    }
}
