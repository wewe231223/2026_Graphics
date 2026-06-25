#include "Game/Environment/EnvironmentRuntime.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include "Core/DX/DesciptorHeap.h"
#include "Utility/DirectXInclude.h"
#include "Utility/ErrorHandler.h"

namespace Game {
    namespace {
        constexpr std::uint32_t EnvironmentGpuRootConstantDwordCount{ 44u };
        constexpr std::uint32_t EnvironmentGpuStatusDwordCount{ 16u };
        constexpr std::uint32_t EnvironmentComputeThreadGroupSize{ 64u };
        constexpr std::uint32_t EnvironmentDrawRecordGpuDrivenFlag{ 0x1u };
        constexpr std::uint32_t InvalidDescriptorIndex{ 0xffffffffu };
        constexpr float EnvironmentGpuCullRadius{ 18.0f };
        constexpr float EnvironmentGpuMaxDrawDistance{ 1000.0f };

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
            std::uint32_t mInstanceContextSrvIndex{};
            std::uint32_t mDrawRecordSrvIndex{};
            std::uint32_t mIndirectArgumentUavIndex{};
            std::uint32_t mVisibleInstanceIndexUavIndex{};
            std::uint32_t mDrawRecordCount{};
            std::uint32_t mVisibleInstanceIndexCapacity{};
            std::uint32_t mMaximumDrawDistance{};
            std::uint32_t mCullingRadius{};
            std::uint32_t mReserved0{};
            std::array<std::uint32_t, 24> mFrustumPlanes{};
        };

        static_assert(sizeof(EnvironmentGpuRootConstants) == sizeof(std::uint32_t) * EnvironmentGpuRootConstantDwordCount);

        struct EnvironmentFrustumPlane final {
        public:
            float mX{};
            float mY{};
            float mZ{};
            float mW{};
        };

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

        EnvironmentFrustumPlane NormalizeEnvironmentFrustumPlane(EnvironmentFrustumPlane Plane) {
            const float LengthSquared{ (Plane.mX * Plane.mX) + (Plane.mY * Plane.mY) + (Plane.mZ * Plane.mZ) };
            if (LengthSquared <= 0.0f) {
                return Plane;
            }

            const float LengthInverse{ 1.0f / std::sqrt(LengthSquared) };
            Plane.mX *= LengthInverse;
            Plane.mY *= LengthInverse;
            Plane.mZ *= LengthInverse;
            Plane.mW *= LengthInverse;
            return Plane;
        }

        std::array<EnvironmentFrustumPlane, 6> BuildEnvironmentFrustumPlanes(const SimpleMath::Matrix& ViewProjection) {
            std::array<EnvironmentFrustumPlane, 6> Planes{};
            Planes[0] = NormalizeEnvironmentFrustumPlane(EnvironmentFrustumPlane{ ViewProjection._11 + ViewProjection._14, ViewProjection._21 + ViewProjection._24, ViewProjection._31 + ViewProjection._34, ViewProjection._41 + ViewProjection._44 });
            Planes[1] = NormalizeEnvironmentFrustumPlane(EnvironmentFrustumPlane{ -ViewProjection._11 + ViewProjection._14, -ViewProjection._21 + ViewProjection._24, -ViewProjection._31 + ViewProjection._34, -ViewProjection._41 + ViewProjection._44 });
            Planes[2] = NormalizeEnvironmentFrustumPlane(EnvironmentFrustumPlane{ ViewProjection._12 + ViewProjection._14, ViewProjection._22 + ViewProjection._24, ViewProjection._32 + ViewProjection._34, ViewProjection._42 + ViewProjection._44 });
            Planes[3] = NormalizeEnvironmentFrustumPlane(EnvironmentFrustumPlane{ -ViewProjection._12 + ViewProjection._14, -ViewProjection._22 + ViewProjection._24, -ViewProjection._32 + ViewProjection._34, -ViewProjection._42 + ViewProjection._44 });
            Planes[4] = NormalizeEnvironmentFrustumPlane(EnvironmentFrustumPlane{ ViewProjection._13, ViewProjection._23, ViewProjection._33, ViewProjection._43 });
            Planes[5] = NormalizeEnvironmentFrustumPlane(EnvironmentFrustumPlane{ -ViewProjection._13 + ViewProjection._14, -ViewProjection._23 + ViewProjection._24, -ViewProjection._33 + ViewProjection._34, -ViewProjection._43 + ViewProjection._44 });
            return Planes;
        }

        bool CompareEnvironmentDrawRecordByPso(const RenderContract::EnvironmentDrawRecord& Left, const RenderContract::EnvironmentDrawRecord& Right) {
            return std::tie(Left.mPass, Left.mPipeline, Left.mMesh, Left.mSubMesh, Left.mSegmentContextIndex, Left.mMaterialIndex, Left.mCastsShadow) < std::tie(Right.mPass, Right.mPipeline, Right.mMesh, Right.mSubMesh, Right.mSegmentContextIndex, Right.mMaterialIndex, Right.mCastsShadow);
        }

        RenderContract::EnvironmentSegmentContext BuildGpuEnvironmentSegmentContext(const RenderContract::EnvironmentSegmentContext& SourceSegmentContext) {
            RenderContract::EnvironmentSegmentContext GpuSegmentContext{ SourceSegmentContext };
            GpuSegmentContext.mLocalTransform = SourceSegmentContext.mLocalTransform.Transpose();
            return GpuSegmentContext;
        }

        std::span<const std::byte> MakeByteSpan(const void* Data, std::size_t SizeInBytes) {
            return std::span<const std::byte>{ static_cast<const std::byte*>(Data), SizeInBytes };
        }

        template <typename T>
        std::span<const std::byte> MakeVectorByteSpan(const std::vector<T>& Values) {
            return std::as_bytes(std::span<const T>{ Values.data(), Values.size() });
        }

        EnvironmentGpuRootConstants BuildEnvironmentGpuRootConstants(const EnvironmentFrameInput& Input, std::uint32_t StatusUavIndex, std::uint32_t InstanceContextSrvIndex, std::uint32_t DrawRecordSrvIndex, std::uint32_t IndirectArgumentUavIndex, std::uint32_t VisibleInstanceIndexUavIndex, std::uint32_t DrawRecordCount, std::uint32_t VisibleInstanceIndexCapacity) {
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
            Constants.mInstanceContextSrvIndex = InstanceContextSrvIndex;
            Constants.mDrawRecordSrvIndex = DrawRecordSrvIndex;
            Constants.mIndirectArgumentUavIndex = IndirectArgumentUavIndex;
            Constants.mVisibleInstanceIndexUavIndex = VisibleInstanceIndexUavIndex;
            Constants.mDrawRecordCount = DrawRecordCount;
            Constants.mVisibleInstanceIndexCapacity = VisibleInstanceIndexCapacity;
            Constants.mMaximumDrawDistance = std::bit_cast<std::uint32_t>(EnvironmentGpuMaxDrawDistance);
            Constants.mCullingRadius = std::bit_cast<std::uint32_t>(EnvironmentGpuCullRadius);
            Constants.mReserved0 = 0u;

            const std::array<EnvironmentFrustumPlane, 6> Planes{ BuildEnvironmentFrustumPlanes(Input.mViewProjection) };
            for (std::size_t PlaneIndex{}; PlaneIndex < Planes.size(); PlaneIndex += 1ULL) {
                const EnvironmentFrustumPlane& Plane{ Planes[PlaneIndex] };
                const std::size_t ConstantIndex{ PlaneIndex * 4ULL };
                Constants.mFrustumPlanes[ConstantIndex + 0ULL] = std::bit_cast<std::uint32_t>(Plane.mX);
                Constants.mFrustumPlanes[ConstantIndex + 1ULL] = std::bit_cast<std::uint32_t>(Plane.mY);
                Constants.mFrustumPlanes[ConstantIndex + 2ULL] = std::bit_cast<std::uint32_t>(Plane.mZ);
                Constants.mFrustumPlanes[ConstantIndex + 3ULL] = std::bit_cast<std::uint32_t>(Plane.mW);
            }

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
        mGpuStatusUavHandle{},
        mLastGpuDispatchFuture{},
        mConfigPath{},
        mGpuInstanceContexts{},
        mGpuSegmentContexts{},
        mGpuDrawRecords{},
        mGpuIndirectArguments{},
        mGpuDrivenFrameResources{},
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
        mGpuStatusUavHandle{ std::move(Other.mGpuStatusUavHandle) },
        mLastGpuDispatchFuture{ std::move(Other.mLastGpuDispatchFuture) },
        mConfigPath{ std::move(Other.mConfigPath) },
        mGpuInstanceContexts{ std::move(Other.mGpuInstanceContexts) },
        mGpuSegmentContexts{ std::move(Other.mGpuSegmentContexts) },
        mGpuDrawRecords{ std::move(Other.mGpuDrawRecords) },
        mGpuIndirectArguments{ std::move(Other.mGpuIndirectArguments) },
        mGpuDrivenFrameResources{ std::move(Other.mGpuDrivenFrameResources) },
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
        Other.mGpuDrivenFrameResources = {};
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
        mGpuStatusUavHandle = std::move(Other.mGpuStatusUavHandle);
        mLastGpuDispatchFuture = std::move(Other.mLastGpuDispatchFuture);
        mConfigPath = std::move(Other.mConfigPath);
        mGpuInstanceContexts = std::move(Other.mGpuInstanceContexts);
        mGpuSegmentContexts = std::move(Other.mGpuSegmentContexts);
        mGpuDrawRecords = std::move(Other.mGpuDrawRecords);
        mGpuIndirectArguments = std::move(Other.mGpuIndirectArguments);
        mGpuDrivenFrameResources = std::move(Other.mGpuDrivenFrameResources);
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
        Other.mGpuDrivenFrameResources = {};
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

    RenderContract::Future EnvironmentRuntime::PrepareGpuDrivenFrame(const EnvironmentFrameInput& Input, RenderContract::RenderFrameData& RenderData) {
        RenderData.mEnvironmentGpuDrivenFrame = RenderContract::EnvironmentGpuDrivenFrameData{};
        if (mInitialized == false || mGpuDrivenEnabled == false || mGpuResourcesInitialized == false || mCopyQueue == nullptr || mComputeQueue == nullptr || RenderData.mEnvironmentDrawRecords.empty() == true || RenderData.mEnvironmentInstanceContexts.empty() == true) {
            mLastGpuDispatchFuture = RenderContract::Future{};
            return mLastGpuDispatchFuture;
        }

        std::stable_sort(RenderData.mEnvironmentDrawRecords.begin(), RenderData.mEnvironmentDrawRecords.end(), CompareEnvironmentDrawRecordByPso);

        std::uint32_t VisibleInstanceIndexCount{};
        BuildGpuDrivenFrameData(RenderData, VisibleInstanceIndexCount);
        if (mGpuDrawRecords.empty() == true || VisibleInstanceIndexCount == 0u) {
            mLastGpuDispatchFuture = RenderContract::Future{};
            return mLastGpuDispatchFuture;
        }

        const std::uint32_t InstanceContextCount{ static_cast<std::uint32_t>(mGpuInstanceContexts.size()) };
        const std::uint32_t SegmentContextCount{ static_cast<std::uint32_t>(mGpuSegmentContexts.size()) };
        const std::uint32_t DrawRecordCount{ static_cast<std::uint32_t>(mGpuDrawRecords.size()) };
        const std::size_t FrameResourceIndex{ static_cast<std::size_t>(Input.mFrameIndex % Constants::FrameCount<std::uint64_t>) };
        EnvironmentGpuDrivenFrameResource& FrameResource{ mGpuDrivenFrameResources[FrameResourceIndex] };
        if (EnsureGpuDrivenFrameResources(FrameResource, InstanceContextCount, SegmentContextCount, DrawRecordCount, VisibleInstanceIndexCount) == false) {
            mLastGpuDispatchFuture = RenderContract::Future{};
            return mLastGpuDispatchFuture;
        }

        UpdateGpuDrivenShaderResourceViews(FrameResource, InstanceContextCount, SegmentContextCount, DrawRecordCount, VisibleInstanceIndexCount);
        UpdateGpuDrivenUnorderedAccessViews(FrameResource, VisibleInstanceIndexCount, DrawRecordCount);

        const RenderContract::Future CopyFuture{ UploadGpuDrivenFrameData(FrameResource) };
        mLastGpuDispatchFuture = DispatchGpuDrivenFrame(FrameResource, Input, CopyFuture, DrawRecordCount, VisibleInstanceIndexCount);
        FillGpuDrivenFramePayload(FrameResource, RenderData, mLastGpuDispatchFuture);
        return mLastGpuDispatchFuture;
    }

    RenderContract::Future EnvironmentRuntime::DispatchGpu(const EnvironmentFrameInput& Input) {
        if (mInitialized == false || mGpuDrivenEnabled == false || mGpuResourcesInitialized == false || mComputeQueue == nullptr || mSrvHeap == nullptr || mGpuStatusUavIndex == InvalidDescriptorIndex) {
            mLastGpuDispatchFuture = RenderContract::Future{};
            return mLastGpuDispatchFuture;
        }

        const EnvironmentGpuRootConstants RootConstants{ BuildEnvironmentGpuRootConstants(Input, mGpuStatusUavIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, 0u, 0u) };

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

        mGpuStatusUavHandle = mSrvHeap->Allocate();
        if (mGpuStatusUavHandle.IsValid() == false) {
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

        mDevice->CreateUnorderedAccessView(mGpuStatusBuffer.Get(), nullptr, &UavDesc, mGpuStatusUavHandle.GetCPU());
        mGpuStatusUavIndex = mGpuStatusUavHandle.GetIndex();
        return true;
    }

    bool EnvironmentRuntime::EnsureGpuDrivenDescriptorHandles(EnvironmentGpuDrivenFrameResource& FrameResource) {
        if (mSrvHeap == nullptr) {
            return false;
        }

        if (FrameResource.mInstanceContextSrvHandle.IsValid() == false) {
            FrameResource.mInstanceContextSrvHandle = mSrvHeap->Allocate();
        }

        if (FrameResource.mSegmentContextSrvHandle.IsValid() == false) {
            FrameResource.mSegmentContextSrvHandle = mSrvHeap->Allocate();
        }

        if (FrameResource.mDrawRecordSrvHandle.IsValid() == false) {
            FrameResource.mDrawRecordSrvHandle = mSrvHeap->Allocate();
        }

        if (FrameResource.mVisibleInstanceIndexSrvHandle.IsValid() == false) {
            FrameResource.mVisibleInstanceIndexSrvHandle = mSrvHeap->Allocate();
        }

        if (FrameResource.mVisibleInstanceIndexUavHandle.IsValid() == false) {
            FrameResource.mVisibleInstanceIndexUavHandle = mSrvHeap->Allocate();
        }

        if (FrameResource.mIndirectArgumentUavHandle.IsValid() == false) {
            FrameResource.mIndirectArgumentUavHandle = mSrvHeap->Allocate();
        }

        return FrameResource.mInstanceContextSrvHandle.IsValid() == true && FrameResource.mSegmentContextSrvHandle.IsValid() == true && FrameResource.mDrawRecordSrvHandle.IsValid() == true && FrameResource.mVisibleInstanceIndexSrvHandle.IsValid() == true && FrameResource.mVisibleInstanceIndexUavHandle.IsValid() == true && FrameResource.mIndirectArgumentUavHandle.IsValid() == true;
    }

    bool EnvironmentRuntime::EnsureGpuDrivenBuffer(Microsoft::WRL::ComPtr<ID3D12Resource>& Buffer, std::uint64_t& CapacityInBytes, std::uint64_t RequiredSizeInBytes, D3D12_RESOURCE_FLAGS ResourceFlags, D3D12_RESOURCE_STATES InitialState, const wchar_t* ResourceName) {
        if (mDevice == nullptr || RequiredSizeInBytes == 0u) {
            return false;
        }

        if (Buffer != nullptr && CapacityInBytes >= RequiredSizeInBytes) {
            return true;
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
        ResourceDesc.Width = RequiredSizeInBytes;
        ResourceDesc.Height = 1u;
        ResourceDesc.DepthOrArraySize = 1u;
        ResourceDesc.MipLevels = 1u;
        ResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        ResourceDesc.SampleDesc.Count = 1u;
        ResourceDesc.SampleDesc.Quality = 0u;
        ResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ResourceDesc.Flags = ResourceFlags;

        Microsoft::WRL::ComPtr<ID3D12Resource> NewBuffer{};
        HRESULT CreateResult{ mDevice->CreateCommittedResource(&HeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDesc, InitialState, nullptr, IID_PPV_ARGS(NewBuffer.GetAddressOf())) };
        if (FAILED(CreateResult) == true || NewBuffer == nullptr) {
            ErrorHandler::report("EnvironmentRuntime", "Failed to create environment GPU driven buffer.", ErrorHandler::Level::Warning);
            return false;
        }

        if (ResourceName != nullptr) {
            NewBuffer->SetName(ResourceName);
        }

        Buffer = std::move(NewBuffer);
        CapacityInBytes = RequiredSizeInBytes;
        return true;
    }

    bool EnvironmentRuntime::EnsureGpuDrivenFrameResources(EnvironmentGpuDrivenFrameResource& FrameResource, std::uint32_t InstanceContextCount, std::uint32_t SegmentContextCount, std::uint32_t DrawRecordCount, std::uint32_t VisibleInstanceIndexCount) {
        if (EnsureGpuDrivenDescriptorHandles(FrameResource) == false) {
            return false;
        }

        const std::uint32_t SafeInstanceContextCount{ std::max(InstanceContextCount, 1u) };
        const std::uint32_t SafeSegmentContextCount{ std::max(SegmentContextCount, 1u) };
        const std::uint32_t SafeDrawRecordCount{ std::max(DrawRecordCount, 1u) };
        const std::uint32_t SafeVisibleInstanceIndexCount{ std::max(VisibleInstanceIndexCount, 1u) };
        ID3D12Resource* PreviousVisibleInstanceIndexBuffer{ FrameResource.mVisibleInstanceIndexBuffer.Get() };
        ID3D12Resource* PreviousIndirectArgumentBuffer{ FrameResource.mIndirectArgumentBuffer.Get() };

        bool Result{ true };
        Result = EnsureGpuDrivenBuffer(FrameResource.mInstanceContextBuffer, FrameResource.mInstanceContextBufferCapacityInBytes, sizeof(RenderContract::EnvironmentInstanceContext) * static_cast<std::uint64_t>(SafeInstanceContextCount), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.InstanceContextBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(FrameResource.mSegmentContextBuffer, FrameResource.mSegmentContextBufferCapacityInBytes, sizeof(RenderContract::EnvironmentSegmentContext) * static_cast<std::uint64_t>(SafeSegmentContextCount), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.SegmentContextBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(FrameResource.mDrawRecordBuffer, FrameResource.mDrawRecordBufferCapacityInBytes, sizeof(RenderContract::EnvironmentDrawRecordGpu) * static_cast<std::uint64_t>(SafeDrawRecordCount), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.DrawRecordBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(FrameResource.mVisibleInstanceIndexBuffer, FrameResource.mVisibleInstanceIndexBufferCapacityInBytes, sizeof(std::uint32_t) * static_cast<std::uint64_t>(SafeVisibleInstanceIndexCount), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.VisibleInstanceIndexBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(FrameResource.mIndirectArgumentBuffer, FrameResource.mIndirectArgumentBufferCapacityInBytes, sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) * static_cast<std::uint64_t>(SafeDrawRecordCount), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.IndirectArgumentBuffer") && Result;
        if (FrameResource.mVisibleInstanceIndexBuffer.Get() != PreviousVisibleInstanceIndexBuffer) {
            FrameResource.mVisibleInstanceIndexState = D3D12_RESOURCE_STATE_COMMON;
        }

        if (FrameResource.mIndirectArgumentBuffer.Get() != PreviousIndirectArgumentBuffer) {
            FrameResource.mIndirectArgumentState = D3D12_RESOURCE_STATE_COMMON;
        }

        return Result;
    }

    void EnvironmentRuntime::BuildGpuDrivenFrameData(RenderContract::RenderFrameData& RenderData, std::uint32_t& OutVisibleInstanceIndexCount) {
        mGpuInstanceContexts = RenderData.mEnvironmentInstanceContexts;
        mGpuSegmentContexts.clear();
        mGpuSegmentContexts.reserve(RenderData.mEnvironmentSegmentContexts.size());
        for (const RenderContract::EnvironmentSegmentContext& SegmentContext : RenderData.mEnvironmentSegmentContexts) {
            mGpuSegmentContexts.push_back(BuildGpuEnvironmentSegmentContext(SegmentContext));
        }

        mGpuDrawRecords.clear();
        mGpuDrawRecords.resize(RenderData.mEnvironmentDrawRecords.size());
        mGpuIndirectArguments.clear();
        mGpuIndirectArguments.resize(RenderData.mEnvironmentDrawRecords.size());

        std::uint32_t VisibleInstanceOffset{};
        for (std::size_t DrawRecordIndex{}; DrawRecordIndex < RenderData.mEnvironmentDrawRecords.size(); DrawRecordIndex += 1ULL) {
            const RenderContract::EnvironmentDrawRecord& SourceRecord{ RenderData.mEnvironmentDrawRecords[DrawRecordIndex] };
            RenderContract::EnvironmentDrawRecordGpu& DestinationRecord{ mGpuDrawRecords[DrawRecordIndex] };
            DestinationRecord.mInstanceOffset = SourceRecord.mInstanceOffset;
            DestinationRecord.mInstanceCount = SourceRecord.mInstanceCount;
            DestinationRecord.mSegmentContextIndex = SourceRecord.mSegmentContextIndex;
            DestinationRecord.mMaterialIndex = SourceRecord.mMaterialIndex;
            DestinationRecord.mFlags = SourceRecord.mFlags;
            DestinationRecord.mVisibleInstanceOffset = VisibleInstanceOffset;
            DestinationRecord.mGpuDrivenFlags = EnvironmentDrawRecordGpuDrivenFlag;
            DestinationRecord.mPadding2 = 0u;

            D3D12_DRAW_INDEXED_ARGUMENTS& DrawArguments{ mGpuIndirectArguments[DrawRecordIndex] };
            if (SourceRecord.mMesh != nullptr && SourceRecord.mInstanceCount > 0u) {
                const Game::ModelSubMesh& SubMesh{ SourceRecord.mMesh->GetSubMesh(SourceRecord.mSubMesh) };
                DrawArguments.IndexCountPerInstance = static_cast<UINT>(SubMesh.mIndexCount);
                DrawArguments.InstanceCount = 0u;
                DrawArguments.StartIndexLocation = static_cast<UINT>(SubMesh.mIndexOffset);
                DrawArguments.BaseVertexLocation = 0;
                DrawArguments.StartInstanceLocation = 0u;
            }

            VisibleInstanceOffset += SourceRecord.mInstanceCount;
        }

        OutVisibleInstanceIndexCount = VisibleInstanceOffset;
    }

    RenderContract::Future EnvironmentRuntime::UploadGpuDrivenFrameData(EnvironmentGpuDrivenFrameResource& FrameResource) {
        if (mCopyQueue == nullptr || FrameResource.mInstanceContextBuffer == nullptr || FrameResource.mSegmentContextBuffer == nullptr || FrameResource.mDrawRecordBuffer == nullptr || FrameResource.mIndirectArgumentBuffer == nullptr) {
            return RenderContract::Future{};
        }

        std::vector<Interface::CopyRequest> CopyRequests{};
        CopyRequests.reserve(4ULL);

        Interface::CopyRequest InstanceContextCopyRequest{ Interface::CopyPriority::High };
        InstanceContextCopyRequest.DestinationDefaultResource = FrameResource.mInstanceContextBuffer;
        InstanceContextCopyRequest.DestinationOffset = 0u;
        InstanceContextCopyRequest.SourceData = MakeVectorByteSpan(mGpuInstanceContexts);
        CopyRequests.push_back(InstanceContextCopyRequest);

        Interface::CopyRequest SegmentContextCopyRequest{ Interface::CopyPriority::High };
        SegmentContextCopyRequest.DestinationDefaultResource = FrameResource.mSegmentContextBuffer;
        SegmentContextCopyRequest.DestinationOffset = 0u;
        SegmentContextCopyRequest.SourceData = MakeVectorByteSpan(mGpuSegmentContexts);
        CopyRequests.push_back(SegmentContextCopyRequest);

        Interface::CopyRequest DrawRecordCopyRequest{ Interface::CopyPriority::High };
        DrawRecordCopyRequest.DestinationDefaultResource = FrameResource.mDrawRecordBuffer;
        DrawRecordCopyRequest.DestinationOffset = 0u;
        DrawRecordCopyRequest.SourceData = MakeVectorByteSpan(mGpuDrawRecords);
        CopyRequests.push_back(DrawRecordCopyRequest);

        Interface::CopyRequest IndirectArgumentCopyRequest{ Interface::CopyPriority::High };
        IndirectArgumentCopyRequest.DestinationDefaultResource = FrameResource.mIndirectArgumentBuffer;
        IndirectArgumentCopyRequest.DestinationOffset = 0u;
        IndirectArgumentCopyRequest.SourceData = MakeByteSpan(mGpuIndirectArguments.data(), sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) * mGpuIndirectArguments.size());
        CopyRequests.push_back(IndirectArgumentCopyRequest);

        RenderContract::Future CopyFuture{ mCopyQueue->EnqueueCopyFuture(CopyRequests) };
        mCopyQueue->DispatchCopies();
        return CopyFuture;
    }

    RenderContract::Future EnvironmentRuntime::DispatchGpuDrivenFrame(EnvironmentGpuDrivenFrameResource& FrameResource, const EnvironmentFrameInput& Input, const RenderContract::Future& CopyFuture, std::uint32_t DrawRecordCount, std::uint32_t VisibleInstanceIndexCapacity) {
        if (mComputeQueue == nullptr || mSrvHeap == nullptr || DrawRecordCount == 0u || mGpuStatusUavIndex == InvalidDescriptorIndex || FrameResource.mInstanceContextSrvHandle.IsValid() == false || FrameResource.mDrawRecordSrvHandle.IsValid() == false || FrameResource.mIndirectArgumentUavHandle.IsValid() == false || FrameResource.mVisibleInstanceIndexUavHandle.IsValid() == false) {
            return RenderContract::Future{};
        }

        Microsoft::WRL::ComPtr<ID3D12Resource> VisibleInstanceIndexBuffer{ FrameResource.mVisibleInstanceIndexBuffer };
        Microsoft::WRL::ComPtr<ID3D12Resource> IndirectArgumentBuffer{ FrameResource.mIndirectArgumentBuffer };
        const D3D12_RESOURCE_STATES VisibleInstanceIndexStateBeforeDispatch{ FrameResource.mVisibleInstanceIndexState };
        const D3D12_RESOURCE_STATES IndirectArgumentStateBeforeDispatch{ FrameResource.mIndirectArgumentState };
        const EnvironmentGpuRootConstants RootConstants{ BuildEnvironmentGpuRootConstants(Input, mGpuStatusUavIndex, FrameResource.mInstanceContextSrvHandle.GetIndex(), FrameResource.mDrawRecordSrvHandle.GetIndex(), FrameResource.mIndirectArgumentUavHandle.GetIndex(), FrameResource.mVisibleInstanceIndexUavHandle.GetIndex(), DrawRecordCount, VisibleInstanceIndexCapacity) };

        Interface::ComputeQueueDispatchRequest DispatchRequest{};
        DispatchRequest.WaitFuture = CopyFuture;
        DispatchRequest.RootSignature = mComputeRootSignature;
        DispatchRequest.PipelineState = mPreparePipelineState;
        DispatchRequest.DescriptorHeaps = std::vector<ID3D12DescriptorHeap*>{ mSrvHeap->GetHeap() };
        DispatchRequest.RecordCommands = [VisibleInstanceIndexBuffer, IndirectArgumentBuffer, RootConstants, VisibleInstanceIndexStateBeforeDispatch, IndirectArgumentStateBeforeDispatch](ID3D12GraphicsCommandList* CommandList) {
            if (CommandList == nullptr) {
                return;
            }

            std::array<D3D12_RESOURCE_BARRIER, 2> Barriers{};
            std::uint32_t BarrierCount{};
            if (VisibleInstanceIndexBuffer != nullptr && VisibleInstanceIndexStateBeforeDispatch != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
                D3D12_RESOURCE_BARRIER& Barrier{ Barriers[BarrierCount] };
                Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                Barrier.Transition.pResource = VisibleInstanceIndexBuffer.Get();
                Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                Barrier.Transition.StateBefore = VisibleInstanceIndexStateBeforeDispatch;
                Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                BarrierCount += 1u;
            }

            if (IndirectArgumentBuffer != nullptr && IndirectArgumentStateBeforeDispatch != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
                D3D12_RESOURCE_BARRIER& Barrier{ Barriers[BarrierCount] };
                Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                Barrier.Transition.pResource = IndirectArgumentBuffer.Get();
                Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                Barrier.Transition.StateBefore = IndirectArgumentStateBeforeDispatch;
                Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                BarrierCount += 1u;
            }

            if (BarrierCount > 0u) {
                CommandList->ResourceBarrier(BarrierCount, Barriers.data());
            }

            CommandList->SetComputeRoot32BitConstants(0, EnvironmentGpuRootConstantDwordCount, &RootConstants, 0);
        };
        DispatchRequest.ThreadGroupCountX = DrawRecordCount;
        DispatchRequest.ThreadGroupCountY = 1u;
        DispatchRequest.ThreadGroupCountZ = 1u;

        RenderContract::Future GpuFuture{ mComputeQueue->EnqueueComputeFuture(DispatchRequest) };
        mComputeQueue->DispatchComputes();
        FrameResource.mVisibleInstanceIndexState = D3D12_RESOURCE_STATE_COMMON;
        FrameResource.mIndirectArgumentState = D3D12_RESOURCE_STATE_COMMON;
        return GpuFuture;
    }

    void EnvironmentRuntime::FillGpuDrivenFramePayload(EnvironmentGpuDrivenFrameResource& FrameResource, RenderContract::RenderFrameData& RenderData, const RenderContract::Future& GpuDispatchFuture) {
        RenderContract::EnvironmentGpuDrivenFrameData Payload{};
        Payload.mGpuDispatchFuture = GpuDispatchFuture;
        Payload.mInstanceContextResource = FrameResource.mInstanceContextBuffer.Get();
        Payload.mSegmentContextResource = FrameResource.mSegmentContextBuffer.Get();
        Payload.mDrawRecordResource = FrameResource.mDrawRecordBuffer.Get();
        Payload.mVisibleInstanceIndexResource = FrameResource.mVisibleInstanceIndexBuffer.Get();
        Payload.mIndirectArgumentResource = FrameResource.mIndirectArgumentBuffer.Get();
        Payload.mInstanceContextSrvIndex = FrameResource.mInstanceContextSrvHandle.GetIndex();
        Payload.mSegmentContextSrvIndex = FrameResource.mSegmentContextSrvHandle.GetIndex();
        Payload.mDrawRecordSrvIndex = FrameResource.mDrawRecordSrvHandle.GetIndex();
        Payload.mVisibleInstanceIndexSrvIndex = FrameResource.mVisibleInstanceIndexSrvHandle.GetIndex();
        Payload.mDrawRecordCount = static_cast<std::uint32_t>(mGpuDrawRecords.size());
        Payload.mInstanceContextCount = static_cast<std::uint32_t>(mGpuInstanceContexts.size());
        Payload.mEnabled = GpuDispatchFuture.IsValid() == true && Payload.mInstanceContextResource != nullptr && Payload.mSegmentContextResource != nullptr && Payload.mDrawRecordResource != nullptr && Payload.mVisibleInstanceIndexResource != nullptr && Payload.mIndirectArgumentResource != nullptr;
        RenderData.mEnvironmentGpuDrivenFrame = Payload;
    }

    void EnvironmentRuntime::UpdateGpuDrivenShaderResourceViews(EnvironmentGpuDrivenFrameResource& FrameResource, std::uint32_t InstanceContextCount, std::uint32_t SegmentContextCount, std::uint32_t DrawRecordCount, std::uint32_t VisibleInstanceIndexCount) {
        if (mDevice == nullptr) {
            return;
        }

        const std::uint32_t SafeInstanceContextCount{ std::max(InstanceContextCount, 1u) };
        const std::uint32_t SafeSegmentContextCount{ std::max(SegmentContextCount, 1u) };
        const std::uint32_t SafeDrawRecordCount{ std::max(DrawRecordCount, 1u) };
        const std::uint32_t SafeVisibleInstanceIndexCount{ std::max(VisibleInstanceIndexCount, 1u) };

        D3D12_SHADER_RESOURCE_VIEW_DESC SrvDesc{};
        SrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        SrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        SrvDesc.Format = DXGI_FORMAT_UNKNOWN;
        SrvDesc.Buffer.FirstElement = 0u;
        SrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        SrvDesc.Buffer.NumElements = SafeInstanceContextCount;
        SrvDesc.Buffer.StructureByteStride = sizeof(RenderContract::EnvironmentInstanceContext);
        mDevice->CreateShaderResourceView(FrameResource.mInstanceContextBuffer.Get(), &SrvDesc, FrameResource.mInstanceContextSrvHandle.GetCPU());

        SrvDesc.Buffer.NumElements = SafeSegmentContextCount;
        SrvDesc.Buffer.StructureByteStride = sizeof(RenderContract::EnvironmentSegmentContext);
        mDevice->CreateShaderResourceView(FrameResource.mSegmentContextBuffer.Get(), &SrvDesc, FrameResource.mSegmentContextSrvHandle.GetCPU());

        SrvDesc.Buffer.NumElements = SafeDrawRecordCount;
        SrvDesc.Buffer.StructureByteStride = sizeof(RenderContract::EnvironmentDrawRecordGpu);
        mDevice->CreateShaderResourceView(FrameResource.mDrawRecordBuffer.Get(), &SrvDesc, FrameResource.mDrawRecordSrvHandle.GetCPU());

        SrvDesc.Buffer.NumElements = SafeVisibleInstanceIndexCount;
        SrvDesc.Buffer.StructureByteStride = sizeof(std::uint32_t);
        mDevice->CreateShaderResourceView(FrameResource.mVisibleInstanceIndexBuffer.Get(), &SrvDesc, FrameResource.mVisibleInstanceIndexSrvHandle.GetCPU());
    }

    void EnvironmentRuntime::UpdateGpuDrivenUnorderedAccessViews(EnvironmentGpuDrivenFrameResource& FrameResource, std::uint32_t VisibleInstanceIndexCount, std::uint32_t IndirectArgumentCount) {
        if (mDevice == nullptr) {
            return;
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC UavDesc{};
        UavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        UavDesc.Format = DXGI_FORMAT_UNKNOWN;
        UavDesc.Buffer.FirstElement = 0u;
        UavDesc.Buffer.CounterOffsetInBytes = 0u;
        UavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

        UavDesc.Buffer.NumElements = std::max(VisibleInstanceIndexCount, 1u);
        UavDesc.Buffer.StructureByteStride = sizeof(std::uint32_t);
        mDevice->CreateUnorderedAccessView(FrameResource.mVisibleInstanceIndexBuffer.Get(), nullptr, &UavDesc, FrameResource.mVisibleInstanceIndexUavHandle.GetCPU());

        UavDesc.Buffer.NumElements = std::max(IndirectArgumentCount, 1u);
        UavDesc.Buffer.StructureByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        mDevice->CreateUnorderedAccessView(FrameResource.mIndirectArgumentBuffer.Get(), nullptr, &UavDesc, FrameResource.mIndirectArgumentUavHandle.GetCPU());
    }

    void EnvironmentRuntime::ResetGpuResources() {
        mComputeRootSignature.Reset();
        mPreparePipelineState.Reset();
        mGpuStatusBuffer.Reset();
        mGpuStatusUavHandle = Core::DX::DescriptorHandle{};
        mGpuInstanceContexts.clear();
        mGpuSegmentContexts.clear();
        mGpuDrawRecords.clear();
        mGpuIndirectArguments.clear();
        mGpuDrivenFrameResources = {};
        mGpuStatusUavIndex = InvalidDescriptorIndex;
        mLastGpuDispatchFuture = RenderContract::Future{};
        mGpuDrivenEnabled = false;
        mGpuResourcesInitialized = false;
    }
}
