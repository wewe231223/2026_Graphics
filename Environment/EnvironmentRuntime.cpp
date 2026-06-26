#include "Environment/EnvironmentRuntime.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include "Core/DX/DesciptorHeap.h"
#include "Environment/EnvironmentFoliageRuntime.h"
#include "RenderContract/Shadow/ShadowRenderContext.h"
#include "Utility/DirectXInclude.h"
#include "Utility/ErrorHandler.h"

namespace Game {
    namespace {
        constexpr std::uint32_t EnvironmentGpuRootConstantDwordCount{ 52u };
        constexpr std::uint32_t EnvironmentGpuStatusDwordCount{ 16u };
        constexpr std::uint32_t EnvironmentComputeThreadGroupSize{ 64u };
        constexpr std::uint32_t EnvironmentDrawRecordGpuDrivenFlag{ 0x1u };
        constexpr std::uint32_t InvalidDescriptorIndex{ 0xffffffffu };
        constexpr float EnvironmentGpuCullRadius{ 18.0f };
        constexpr float EnvironmentGpuMaxDrawDistance{ 1000.0f };

        struct DrawRootConstantsB1 final {
        public:
            std::uint32_t mFrameGlobalsSrvIndex{};
            std::uint32_t mModelContextSrvIndex{};
            std::uint32_t mBonePaletteSrvIndex{};
            std::uint32_t mDrawRecordSrvIndex{};
            std::uint32_t mDrawRecordBaseIndex{};
            std::uint32_t mMaterialSrvIndex{};
            std::uint32_t mMaterialTextureTableSrvIndex{};
            std::uint32_t mShadowMappingParameterSrvIndex{};
            std::uint32_t mShadowMapTextureBaseSrvIndex{};
            std::uint32_t mFrameGlobalsElementIndex{};
            std::uint32_t mTerrainPatchContextSrvIndex{};
            std::uint32_t mReserved1{};
        };

        struct EnvironmentGpuRootConstants final {
        public:
            std::uint32_t mStatusUavIndex{};
            std::uint32_t mFrameIndexLow{};
            std::uint32_t mFrameIndexHigh{};
            std::uint32_t mTerrainHeightSrvIndex{};
            std::uint32_t mTerrainSplatSrvIndex{};
            std::uint32_t mTerrainSplat1SrvIndex{};
            std::uint32_t mTerrainWidth{};
            std::uint32_t mTerrainHeight{};
            std::uint32_t mFocusPositionX{};
            std::uint32_t mFocusPositionY{};
            std::uint32_t mFocusPositionZ{};
            std::uint32_t mDispatchThreadGroupSize{};
            std::uint32_t mInstanceContextSrvIndex{};
            std::uint32_t mInstanceContextUavIndex{};
            std::uint32_t mDrawRecordSrvIndex{};
            std::uint32_t mPlacementConfigSrvIndex{};
            std::uint32_t mPlacementRuleSrvIndex{};
            std::uint32_t mPlacementDrawRecordSrvIndex{};
            std::uint32_t mIndirectArgumentUavIndex{};
            std::uint32_t mVisibleInstanceIndexUavIndex{};
            std::uint32_t mDrawRecordCount{};
            std::uint32_t mVisibleInstanceIndexCapacity{};
            std::uint32_t mMaximumDrawDistance{};
            std::uint32_t mCullingRadius{};
            std::uint32_t mPlacementCandidateRecordSrvIndex{};
            std::uint32_t mCandidateContextSrvIndex{};
            std::uint32_t mCandidateContextUavIndex{};
            std::uint32_t mCandidateRecordCount{};
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

        bool CreateEnvironmentComputePipelineState(ID3D12Device* Device, ID3D12RootSignature* RootSignature, const std::string& Identifier, Microsoft::WRL::ComPtr<ID3D12PipelineState>& OutPipelineState) {
            if (Device == nullptr || RootSignature == nullptr) {
                return false;
            }

            std::vector<std::uint8_t> ShaderByteCode{ LoadShaderByteCode("EnvironmentObjectPrepareShader.hlsl", Identifier) };
            if (ShaderByteCode.empty() == true) {
                ErrorHandler::report("EnvironmentRuntime", "Failed to load EnvironmentObjectPrepareShader byte code.", ErrorHandler::Level::Warning);
                return false;
            }

            D3D12_COMPUTE_PIPELINE_STATE_DESC PipelineDesc{};
            PipelineDesc.pRootSignature = RootSignature;
            PipelineDesc.CS.pShaderBytecode = ShaderByteCode.data();
            PipelineDesc.CS.BytecodeLength = ShaderByteCode.size();
            PipelineDesc.NodeMask = 0u;
            PipelineDesc.CachedPSO = D3D12_CACHED_PIPELINE_STATE{};
            PipelineDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

            HRESULT CreateResult{ Device->CreateComputePipelineState(&PipelineDesc, IID_PPV_ARGS(OutPipelineState.GetAddressOf())) };
            if (FAILED(CreateResult) == true || OutPipelineState == nullptr) {
                ErrorHandler::report("EnvironmentRuntime", "Failed to create environment compute pipeline.", ErrorHandler::Level::Warning);
                return false;
            }

            return true;
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

        bool IsEnvironmentBillboardRecord(const RenderContract::EnvironmentDrawRecord& DrawRecord) {
            return DrawRecord.mPipeline != nullptr && DrawRecord.mPipeline->GetPrimitiveTopology() == D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        }

        struct EnvironmentGpuDrawBuildItem final {
        public:
            RenderContract::EnvironmentDrawRecord mDrawRecord{};
            EnvironmentGpuPlacementDrawRecord mPlacementDrawRecord{};
        };

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

        EnvironmentGpuRootConstants BuildEnvironmentGpuRootConstants(const EnvironmentFrameInput& Input, std::uint32_t StatusUavIndex, std::uint32_t InstanceContextSrvIndex, std::uint32_t InstanceContextUavIndex, std::uint32_t DrawRecordSrvIndex, std::uint32_t PlacementConfigSrvIndex, std::uint32_t PlacementRuleSrvIndex, std::uint32_t PlacementDrawRecordSrvIndex, std::uint32_t PlacementCandidateRecordSrvIndex, std::uint32_t CandidateContextSrvIndex, std::uint32_t CandidateContextUavIndex, std::uint32_t IndirectArgumentUavIndex, std::uint32_t VisibleInstanceIndexUavIndex, std::uint32_t DrawRecordCount, std::uint32_t VisibleInstanceIndexCapacity, std::uint32_t CandidateRecordCount) {
            EnvironmentGpuRootConstants Constants{};
            Constants.mStatusUavIndex = StatusUavIndex;
            Constants.mFrameIndexLow = static_cast<std::uint32_t>(Input.mFrameIndex & 0xffffffffULL);
            Constants.mFrameIndexHigh = static_cast<std::uint32_t>((Input.mFrameIndex >> 32ULL) & 0xffffffffULL);
            Constants.mTerrainHeightSrvIndex = Input.mTerrain.mHeightSrvIndex;
            Constants.mTerrainSplatSrvIndex = Input.mTerrain.mSplatSrvIndex;
            Constants.mTerrainSplat1SrvIndex = Input.mTerrain.mSplat1SrvIndex;
            Constants.mTerrainWidth = Input.mTerrain.mWidth;
            Constants.mTerrainHeight = Input.mTerrain.mHeight;
            Constants.mFocusPositionX = std::bit_cast<std::uint32_t>(Input.mFocusPosition.x);
            Constants.mFocusPositionY = std::bit_cast<std::uint32_t>(Input.mFocusPosition.y);
            Constants.mFocusPositionZ = std::bit_cast<std::uint32_t>(Input.mFocusPosition.z);
            Constants.mDispatchThreadGroupSize = EnvironmentComputeThreadGroupSize;
            Constants.mInstanceContextSrvIndex = InstanceContextSrvIndex;
            Constants.mInstanceContextUavIndex = InstanceContextUavIndex;
            Constants.mDrawRecordSrvIndex = DrawRecordSrvIndex;
            Constants.mPlacementConfigSrvIndex = PlacementConfigSrvIndex;
            Constants.mPlacementRuleSrvIndex = PlacementRuleSrvIndex;
            Constants.mPlacementDrawRecordSrvIndex = PlacementDrawRecordSrvIndex;
            Constants.mIndirectArgumentUavIndex = IndirectArgumentUavIndex;
            Constants.mVisibleInstanceIndexUavIndex = VisibleInstanceIndexUavIndex;
            Constants.mDrawRecordCount = DrawRecordCount;
            Constants.mVisibleInstanceIndexCapacity = VisibleInstanceIndexCapacity;
            Constants.mMaximumDrawDistance = std::bit_cast<std::uint32_t>(EnvironmentGpuMaxDrawDistance);
            Constants.mCullingRadius = std::bit_cast<std::uint32_t>(EnvironmentGpuCullRadius);
            Constants.mPlacementCandidateRecordSrvIndex = PlacementCandidateRecordSrvIndex;
            Constants.mCandidateContextSrvIndex = CandidateContextSrvIndex;
            Constants.mCandidateContextUavIndex = CandidateContextUavIndex;
            Constants.mCandidateRecordCount = CandidateRecordCount;

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
        mCandidateGeneratePipelineState{},
        mCandidateClassifyPipelineState{},
        mDrawIndexedIndirectCommandSignature{},
        mGpuStatusBuffer{},
        mGpuStatusUavHandle{},
        mLastGpuDispatchFuture{},
        mConfigPath{ "Resources/DefaultScene/FoliagePlacement.yaml" },
        mFoliageRuntime{},
        mGpuInstanceContexts{},
        mGpuSegmentContexts{},
        mGpuDrawRecords{},
        mGpuIndirectArguments{},
        mGpuPlacementFrameData{},
        mVertexBufferViewCache{},
        mGpuDrivenFrameResources{},
        mGpuInstanceContextCount{},
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
        mCandidateGeneratePipelineState{ std::move(Other.mCandidateGeneratePipelineState) },
        mCandidateClassifyPipelineState{ std::move(Other.mCandidateClassifyPipelineState) },
        mDrawIndexedIndirectCommandSignature{ std::move(Other.mDrawIndexedIndirectCommandSignature) },
        mGpuStatusBuffer{ std::move(Other.mGpuStatusBuffer) },
        mGpuStatusUavHandle{ std::move(Other.mGpuStatusUavHandle) },
        mLastGpuDispatchFuture{ std::move(Other.mLastGpuDispatchFuture) },
        mConfigPath{ std::move(Other.mConfigPath) },
        mFoliageRuntime{ std::move(Other.mFoliageRuntime) },
        mGpuInstanceContexts{ std::move(Other.mGpuInstanceContexts) },
        mGpuSegmentContexts{ std::move(Other.mGpuSegmentContexts) },
        mGpuDrawRecords{ std::move(Other.mGpuDrawRecords) },
        mGpuIndirectArguments{ std::move(Other.mGpuIndirectArguments) },
        mGpuPlacementFrameData{ std::move(Other.mGpuPlacementFrameData) },
        mVertexBufferViewCache{ std::move(Other.mVertexBufferViewCache) },
        mGpuDrivenFrameResources{ std::move(Other.mGpuDrivenFrameResources) },
        mGpuInstanceContextCount{ Other.mGpuInstanceContextCount },
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
        Other.mDrawIndexedIndirectCommandSignature.Reset();
        Other.mVertexBufferViewCache.clear();
        Other.mGpuDrivenFrameResources = {};
        Other.mGpuPlacementFrameData = EnvironmentGpuPlacementFrameData{};
        Other.mGpuInstanceContextCount = 0u;
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
        mCandidateGeneratePipelineState = std::move(Other.mCandidateGeneratePipelineState);
        mCandidateClassifyPipelineState = std::move(Other.mCandidateClassifyPipelineState);
        mDrawIndexedIndirectCommandSignature = std::move(Other.mDrawIndexedIndirectCommandSignature);
        mGpuStatusBuffer = std::move(Other.mGpuStatusBuffer);
        mGpuStatusUavHandle = std::move(Other.mGpuStatusUavHandle);
        mLastGpuDispatchFuture = std::move(Other.mLastGpuDispatchFuture);
        mConfigPath = std::move(Other.mConfigPath);
        mFoliageRuntime = std::move(Other.mFoliageRuntime);
        mGpuInstanceContexts = std::move(Other.mGpuInstanceContexts);
        mGpuSegmentContexts = std::move(Other.mGpuSegmentContexts);
        mGpuDrawRecords = std::move(Other.mGpuDrawRecords);
        mGpuIndirectArguments = std::move(Other.mGpuIndirectArguments);
        mGpuPlacementFrameData = std::move(Other.mGpuPlacementFrameData);
        mVertexBufferViewCache = std::move(Other.mVertexBufferViewCache);
        mGpuDrivenFrameResources = std::move(Other.mGpuDrivenFrameResources);
        mGpuInstanceContextCount = Other.mGpuInstanceContextCount;
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
        Other.mDrawIndexedIndirectCommandSignature.Reset();
        Other.mVertexBufferViewCache.clear();
        Other.mGpuDrivenFrameResources = {};
        Other.mGpuPlacementFrameData = EnvironmentGpuPlacementFrameData{};
        Other.mGpuInstanceContextCount = 0u;
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
        mFoliageRuntime.reset();
        mLastGpuDispatchFuture = RenderContract::Future{};
        mInitialized = false;
        mGpuDrivenEnabled = false;
    }

    void EnvironmentRuntime::SetConfigPath(const std::string& ConfigPath) {
        mConfigPath = ConfigPath;
        if (mFoliageRuntime != nullptr) {
            mFoliageRuntime->SetConfigPath(mConfigPath);
        }
    }

    const std::string& EnvironmentRuntime::GetConfigPath() const {
        return mConfigPath;
    }

    void EnvironmentRuntime::Tick(Arche::World& World, FrameContext& Ctx, float Dt) {
        Ctx.RenderData.mEnvironmentRuntime = this;
        if (mFoliageRuntime == nullptr) {
            mFoliageRuntime = std::make_unique<EnvironmentFoliageRuntime>(mConfigPath);
        }

        mFoliageRuntime->Update(World, Ctx, Dt, IsGpuDrivenEnabled());
    }

    void EnvironmentRuntime::Tick(const EnvironmentFrameInput& Input, RenderContract::RenderFrameData& RenderData) {
        RenderData.mEnvironmentRuntime = this;
        TickCpu(Input);
        if (IsGpuDrivenEnabled() == false) {
            RenderData.mEnvironmentGpuDrivenFrame = RenderContract::EnvironmentGpuDrivenFrameData{};
            mLastGpuDispatchFuture = RenderContract::Future{};
            return;
        }

        PrepareGpuDrivenFrame(Input, RenderData);
    }

    void EnvironmentRuntime::TickCpu(const EnvironmentFrameInput& Input) {
        static_cast<void>(Input);
    }

    RenderContract::Future EnvironmentRuntime::PrepareGpuDrivenFrame(const EnvironmentFrameInput& Input, RenderContract::RenderFrameData& RenderData) {
        RenderData.mEnvironmentGpuDrivenFrame = RenderContract::EnvironmentGpuDrivenFrameData{};
        RenderData.mEnvironmentInstanceContexts.clear();
        RenderData.mEnvironmentSegmentContexts.clear();
        RenderData.mEnvironmentDrawRecords.clear();
        mGpuPlacementFrameData = EnvironmentGpuPlacementFrameData{};
        if (mFoliageRuntime != nullptr) {
            mFoliageRuntime->BuildGpuDrivenRenderData(Input.mFocusPosition, RenderData, mGpuPlacementFrameData);
        }

        if (mInitialized == false || mGpuDrivenEnabled == false || mGpuResourcesInitialized == false || mCopyQueue == nullptr || mComputeQueue == nullptr || RenderData.mEnvironmentDrawRecords.empty() == true || mGpuPlacementFrameData.mDrawRecords.empty() == true || mGpuPlacementFrameData.mRules.empty() == true || mGpuPlacementFrameData.mCandidateRecords.empty() == true || mGpuPlacementFrameData.mCandidateCount == 0u || Input.mTerrain.mHeightSrvIndex == InvalidDescriptorIndex || Input.mTerrain.mSplatSrvIndex == InvalidDescriptorIndex || Input.mTerrain.mSplat1SrvIndex == InvalidDescriptorIndex) {
            mLastGpuDispatchFuture = RenderContract::Future{};
            return mLastGpuDispatchFuture;
        }

        std::uint32_t VisibleInstanceIndexCount{};
        BuildGpuDrivenFrameData(Input, RenderData, VisibleInstanceIndexCount);
        if (mGpuDrawRecords.empty() == true || VisibleInstanceIndexCount == 0u) {
            mLastGpuDispatchFuture = RenderContract::Future{};
            return mLastGpuDispatchFuture;
        }

        const std::uint32_t InstanceContextCount{ mGpuInstanceContextCount };
        const std::uint32_t SegmentContextCount{ static_cast<std::uint32_t>(mGpuSegmentContexts.size()) };
        const std::uint32_t DrawRecordCount{ static_cast<std::uint32_t>(mGpuDrawRecords.size()) };
        const std::uint32_t PlacementConfigCount{ 1u };
        const std::uint32_t PlacementRuleCount{ static_cast<std::uint32_t>(mGpuPlacementFrameData.mRules.size()) };
        const std::uint32_t PlacementDrawRecordCount{ static_cast<std::uint32_t>(mGpuPlacementFrameData.mDrawRecords.size()) };
        const std::uint32_t PlacementCandidateRecordCount{ static_cast<std::uint32_t>(mGpuPlacementFrameData.mCandidateRecords.size()) };
        const std::uint32_t CandidateContextCount{ mGpuPlacementFrameData.mCandidateCount };
        const std::size_t FrameResourceIndex{ static_cast<std::size_t>(Input.mFrameIndex % Constants::FrameCount<std::uint64_t>) };
        EnvironmentGpuDrivenFrameResource& FrameResource{ mGpuDrivenFrameResources[FrameResourceIndex] };
        if (EnsureGpuDrivenFrameResources(FrameResource, InstanceContextCount, SegmentContextCount, DrawRecordCount, PlacementConfigCount, PlacementRuleCount, PlacementDrawRecordCount, PlacementCandidateRecordCount, CandidateContextCount, VisibleInstanceIndexCount) == false) {
            mLastGpuDispatchFuture = RenderContract::Future{};
            return mLastGpuDispatchFuture;
        }

        UpdateGpuDrivenShaderResourceViews(FrameResource, InstanceContextCount, SegmentContextCount, DrawRecordCount, PlacementConfigCount, PlacementRuleCount, PlacementDrawRecordCount, PlacementCandidateRecordCount, CandidateContextCount, VisibleInstanceIndexCount);
        UpdateGpuDrivenUnorderedAccessViews(FrameResource, InstanceContextCount, CandidateContextCount, VisibleInstanceIndexCount, DrawRecordCount);

        const RenderContract::Future CopyFuture{ UploadGpuDrivenFrameData(FrameResource) };
        mLastGpuDispatchFuture = DispatchGpuDrivenFrame(FrameResource, Input, CopyFuture, DrawRecordCount, VisibleInstanceIndexCount, PlacementCandidateRecordCount);
        FillGpuDrivenFramePayload(FrameResource, RenderData, mLastGpuDispatchFuture);
        return mLastGpuDispatchFuture;
    }

    RenderContract::Future EnvironmentRuntime::DispatchGpu(const EnvironmentFrameInput& Input) {
        if (mInitialized == false || mGpuDrivenEnabled == false || mGpuResourcesInitialized == false || mComputeQueue == nullptr || mSrvHeap == nullptr || mGpuStatusUavIndex == InvalidDescriptorIndex) {
            mLastGpuDispatchFuture = RenderContract::Future{};
            return mLastGpuDispatchFuture;
        }

        const EnvironmentGpuRootConstants RootConstants{ BuildEnvironmentGpuRootConstants(Input, mGpuStatusUavIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, 0u, 0u, 0u) };

        Interface::ComputeQueueDispatchRequest DispatchRequest{};
        DispatchRequest.RootSignature = mComputeRootSignature;
        DispatchRequest.PipelineState = mCandidateClassifyPipelineState;
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

    void EnvironmentRuntime::RecordGBuffer(const RenderContract::EnvironmentGBufferRenderCommandContext& Context) {
        if (Context.mCommandList == nullptr || Context.mRenderFrameData == nullptr || Context.mRenderFrameData->mEnvironmentDrawRecords.empty() == true) {
            return;
        }

        if (Context.mRenderFrameData->mEnvironmentGpuDrivenFrame.mEnabled == true) {
            RecordGBufferIndirect(Context);
            return;
        }

        RecordGBufferDirect(Context);
    }

    void EnvironmentRuntime::RecordShadowDepth(const RenderContract::EnvironmentShadowDepthRenderCommandContext& Context) {
        if (Context.mCommandList == nullptr || Context.mPipelineProvider == nullptr) {
            return;
        }

        if (Context.mRenderFrameData != nullptr && Context.mRenderFrameData->mEnvironmentGpuDrivenFrame.mEnabled == true) {
            RecordShadowDepthIndirect(Context);
            return;
        }

        if (Context.mShadowRenderContext == nullptr || Context.mShadowRenderContext->mEnvironmentDrawRecords.empty() == true) {
            return;
        }

        const RenderContract::IPipeline* ActivePipeline{ nullptr };
        for (std::size_t DrawRecordIndex{}; DrawRecordIndex < Context.mShadowRenderContext->mEnvironmentDrawRecords.size(); DrawRecordIndex += 1ULL) {
            const RenderContract::EnvironmentDrawRecord& DrawRecord{ Context.mShadowRenderContext->mEnvironmentDrawRecords[DrawRecordIndex] };
            if (DrawRecord.mMesh == nullptr || DrawRecord.mInstanceCount == 0u || DrawRecord.mCastsShadow == false) {
                continue;
            }

            const RenderContract::IPipeline* Pipeline{ IsEnvironmentBillboardRecord(DrawRecord) == true ? Context.mPipelineProvider->ResolveEnvironmentBillboardDepthPipeline() : Context.mPipelineProvider->ResolveEnvironmentObjectDepthPipeline() };
            if (Pipeline == nullptr) {
                continue;
            }

            ActivePipeline = Pipeline->Set(ActivePipeline, Context.mCommandList);
            if (Context.mDynamicDepthBiasCommandList != nullptr) {
                Context.mDynamicDepthBiasCommandList->RSSetDepthBias(Context.mRasterDepthBias, Context.mRasterDepthBiasClamp, Context.mRasterSlopeScaledDepthBias);
            }

            DrawRootConstantsB1 RootConstants{};
            RootConstants.mFrameGlobalsSrvIndex = Context.mFrameGlobalsSrvIndex;
            RootConstants.mModelContextSrvIndex = Context.mEnvironmentInstanceContextSrvIndex;
            RootConstants.mBonePaletteSrvIndex = Context.mEnvironmentSegmentContextSrvIndex;
            RootConstants.mDrawRecordSrvIndex = Context.mEnvironmentDrawRecordSrvIndex;
            RootConstants.mDrawRecordBaseIndex = static_cast<std::uint32_t>(DrawRecordIndex);
            RootConstants.mMaterialSrvIndex = Context.mMaterialSrvIndex;
            RootConstants.mMaterialTextureTableSrvIndex = Context.mMaterialTextureTableSrvIndex;
            RootConstants.mShadowMappingParameterSrvIndex = InvalidDescriptorIndex;
            RootConstants.mShadowMapTextureBaseSrvIndex = InvalidDescriptorIndex;
            RootConstants.mFrameGlobalsElementIndex = Context.mShadowFrameGlobalsIndex;
            RootConstants.mTerrainPatchContextSrvIndex = InvalidDescriptorIndex;
            RootConstants.mReserved1 = 0u;
            Context.mCommandList->SetGraphicsRoot32BitConstants(0, sizeof(DrawRootConstantsB1) / sizeof(std::uint32_t), &RootConstants, 0);

            Context.mCommandList->IASetPrimitiveTopology(Pipeline->GetPrimitiveTopology());

            const std::vector<D3D12_VERTEX_BUFFER_VIEW>& VertexBufferViews{ ResolveVertexBufferViews(*Pipeline, *DrawRecord.mMesh) };
            if (VertexBufferViews.empty() == false) {
                Context.mCommandList->IASetVertexBuffers(0, static_cast<UINT>(VertexBufferViews.size()), VertexBufferViews.data());
            }

            const D3D12_INDEX_BUFFER_VIEW& IndexBufferView{ DrawRecord.mMesh->GetIndexBufferView() };
            Context.mCommandList->IASetIndexBuffer(&IndexBufferView);

            const RenderContract::ModelSubMesh& SubMesh{ DrawRecord.mMesh->GetSubMesh(DrawRecord.mSubMesh) };
            const UINT IndexCountPerInstance{ static_cast<UINT>(SubMesh.mIndexCount) };
            const UINT InstanceCount{ static_cast<UINT>(DrawRecord.mInstanceCount) };
            const UINT StartIndexLocation{ static_cast<UINT>(SubMesh.mIndexOffset) };
            const INT BaseVertexLocation{ 0 };
            const UINT StartInstanceLocation{ 0 };
            Context.mCommandList->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
        }
    }

    RenderContract::Future EnvironmentRuntime::GetEnvironmentGpuFuture() const {
        return mLastGpuDispatchFuture;
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

    bool EnvironmentRuntime::EnsureDrawIndexedIndirectCommandSignature() {
        if (mDrawIndexedIndirectCommandSignature != nullptr) {
            return true;
        }

        if (mDevice == nullptr) {
            return false;
        }

        D3D12_INDIRECT_ARGUMENT_DESC ArgumentDesc{};
        ArgumentDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

        D3D12_COMMAND_SIGNATURE_DESC CommandSignatureDesc{};
        CommandSignatureDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        CommandSignatureDesc.NumArgumentDescs = 1u;
        CommandSignatureDesc.pArgumentDescs = &ArgumentDesc;
        CommandSignatureDesc.NodeMask = 0u;

        const HRESULT CreateResult{ mDevice->CreateCommandSignature(&CommandSignatureDesc, nullptr, IID_PPV_ARGS(mDrawIndexedIndirectCommandSignature.GetAddressOf())) };
        return SUCCEEDED(CreateResult) == true && mDrawIndexedIndirectCommandSignature != nullptr;
    }

    std::vector<D3D12_VERTEX_BUFFER_VIEW> EnvironmentRuntime::BuildVertexBufferViews(const RenderContract::IPipeline& Pipeline, const RenderContract::IModelNode& Mesh) const {
        const std::span<const RenderContract::VertexInputBinding> VertexInputBindings{ Pipeline.GetVertexInputBindings() };
        std::uint32_t MaxInputSlot{};

        for (const RenderContract::VertexInputBinding& VertexInputBinding : VertexInputBindings) {
            if (VertexInputBinding.mInputSlot > MaxInputSlot) {
                MaxInputSlot = VertexInputBinding.mInputSlot;
            }
        }

        std::vector<D3D12_VERTEX_BUFFER_VIEW> VertexBufferViews{};
        VertexBufferViews.resize(VertexInputBindings.empty() == true ? 0ULL : static_cast<std::size_t>(MaxInputSlot + 1u));

        for (const RenderContract::VertexInputBinding& VertexInputBinding : VertexInputBindings) {
            D3D12_VERTEX_BUFFER_VIEW View{};
            const bool IsResolved{ Mesh.TryGetVertexBufferView(VertexInputBinding.mKind, View) };
            if (IsResolved == false) {
                continue;
            }

            VertexBufferViews[VertexInputBinding.mInputSlot] = View;
        }

        return VertexBufferViews;
    }

    const std::vector<D3D12_VERTEX_BUFFER_VIEW>& EnvironmentRuntime::ResolveVertexBufferViews(const RenderContract::IPipeline& Pipeline, const RenderContract::IModelNode& Mesh) {
        const std::pair<const RenderContract::IPipeline*, const RenderContract::IModelNode*> Key{ &Pipeline, &Mesh };
        const std::map<std::pair<const RenderContract::IPipeline*, const RenderContract::IModelNode*>, std::vector<D3D12_VERTEX_BUFFER_VIEW>>::iterator FoundIterator{ mVertexBufferViewCache.find(Key) };
        if (FoundIterator != mVertexBufferViewCache.end()) {
            return FoundIterator->second;
        }

        std::vector<D3D12_VERTEX_BUFFER_VIEW> VertexBufferViews{ BuildVertexBufferViews(Pipeline, Mesh) };
        const std::pair<std::map<std::pair<const RenderContract::IPipeline*, const RenderContract::IModelNode*>, std::vector<D3D12_VERTEX_BUFFER_VIEW>>::iterator, bool> InsertResult{ mVertexBufferViewCache.insert_or_assign(Key, std::move(VertexBufferViews)) };
        return InsertResult.first->second;
    }

    void EnvironmentRuntime::RecordGBufferDirect(const RenderContract::EnvironmentGBufferRenderCommandContext& Context) {
        if (Context.mRenderFrameData == nullptr || Context.mPipelineProvider == nullptr) {
            return;
        }

        const RenderContract::IPipeline* ActivePipeline{ nullptr };
        for (std::size_t DrawRecordIndex{}; DrawRecordIndex < Context.mRenderFrameData->mEnvironmentDrawRecords.size(); DrawRecordIndex += 1ULL) {
            const RenderContract::EnvironmentDrawRecord& DrawRecord{ Context.mRenderFrameData->mEnvironmentDrawRecords[DrawRecordIndex] };
            if (DrawRecord.mMesh == nullptr || DrawRecord.mInstanceCount == 0u) {
                continue;
            }

            const RenderContract::IPipeline* Pipeline{ IsEnvironmentBillboardRecord(DrawRecord) == true ? DrawRecord.mPipeline : Context.mPipelineProvider->ResolveEnvironmentObjectPipeline() };
            if (Pipeline == nullptr) {
                continue;
            }

            ActivePipeline = Pipeline->Set(ActivePipeline, Context.mCommandList);

            DrawRootConstantsB1 RootConstants{};
            RootConstants.mFrameGlobalsSrvIndex = Context.mFrameGlobalsSrvIndex;
            RootConstants.mModelContextSrvIndex = Context.mEnvironmentInstanceContextSrvIndex;
            RootConstants.mBonePaletteSrvIndex = Context.mEnvironmentSegmentContextSrvIndex;
            RootConstants.mDrawRecordSrvIndex = Context.mEnvironmentDrawRecordSrvIndex;
            RootConstants.mDrawRecordBaseIndex = static_cast<std::uint32_t>(DrawRecordIndex);
            RootConstants.mMaterialSrvIndex = Context.mMaterialSrvIndex;
            RootConstants.mMaterialTextureTableSrvIndex = Context.mMaterialTextureTableSrvIndex;
            RootConstants.mShadowMappingParameterSrvIndex = InvalidDescriptorIndex;
            RootConstants.mShadowMapTextureBaseSrvIndex = InvalidDescriptorIndex;
            RootConstants.mFrameGlobalsElementIndex = 0u;
            RootConstants.mTerrainPatchContextSrvIndex = InvalidDescriptorIndex;
            RootConstants.mReserved1 = 0u;
            Context.mCommandList->SetGraphicsRoot32BitConstants(0, sizeof(DrawRootConstantsB1) / sizeof(std::uint32_t), &RootConstants, 0);

            Context.mCommandList->IASetPrimitiveTopology(Pipeline->GetPrimitiveTopology());

            const std::vector<D3D12_VERTEX_BUFFER_VIEW>& VertexBufferViews{ ResolveVertexBufferViews(*Pipeline, *DrawRecord.mMesh) };
            if (VertexBufferViews.empty() == false) {
                Context.mCommandList->IASetVertexBuffers(0, static_cast<UINT>(VertexBufferViews.size()), VertexBufferViews.data());
            }

            const D3D12_INDEX_BUFFER_VIEW& IndexBufferView{ DrawRecord.mMesh->GetIndexBufferView() };
            Context.mCommandList->IASetIndexBuffer(&IndexBufferView);

            const RenderContract::ModelSubMesh& SubMesh{ DrawRecord.mMesh->GetSubMesh(DrawRecord.mSubMesh) };
            const UINT IndexCountPerInstance{ static_cast<UINT>(SubMesh.mIndexCount) };
            const UINT InstanceCount{ static_cast<UINT>(DrawRecord.mInstanceCount) };
            const UINT StartIndexLocation{ static_cast<UINT>(SubMesh.mIndexOffset) };
            const INT BaseVertexLocation{ 0 };
            const UINT StartInstanceLocation{ 0 };
            Context.mCommandList->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
        }
    }

    void EnvironmentRuntime::RecordGBufferIndirect(const RenderContract::EnvironmentGBufferRenderCommandContext& Context) {
        if (Context.mRenderFrameData == nullptr || Context.mPipelineProvider == nullptr) {
            return;
        }

        const RenderContract::EnvironmentGpuDrivenFrameData& GpuFrame{ Context.mRenderFrameData->mEnvironmentGpuDrivenFrame };
        if (GpuFrame.mEnabled == false || GpuFrame.mVisibleInstanceIndexResource == nullptr || GpuFrame.mIndirectArgumentResource == nullptr || EnsureDrawIndexedIndirectCommandSignature() == false) {
            return;
        }

        std::array<D3D12_RESOURCE_BARRIER, 3> Barriers{};
        Barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        Barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        Barriers[0].Transition.pResource = GpuFrame.mInstanceContextResource;
        Barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        Barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        Barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        Barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        Barriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        Barriers[1].Transition.pResource = GpuFrame.mVisibleInstanceIndexResource;
        Barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        Barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        Barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        Barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        Barriers[2].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        Barriers[2].Transition.pResource = GpuFrame.mIndirectArgumentResource;
        Barriers[2].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        Barriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        Barriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        Context.mCommandList->ResourceBarrier(static_cast<UINT>(Barriers.size()), Barriers.data());

        const RenderContract::IPipeline* ActivePipeline{ nullptr };
        const std::size_t DrawRecordCount{ std::min<std::size_t>(Context.mRenderFrameData->mEnvironmentDrawRecords.size(), GpuFrame.mDrawRecordCount) };
        for (std::size_t DrawRecordIndex{}; DrawRecordIndex < DrawRecordCount; DrawRecordIndex += 1ULL) {
            const RenderContract::EnvironmentDrawRecord& DrawRecord{ Context.mRenderFrameData->mEnvironmentDrawRecords[DrawRecordIndex] };
            if (DrawRecord.mMesh == nullptr || DrawRecord.mInstanceCount == 0u) {
                continue;
            }

            const RenderContract::IPipeline* Pipeline{ IsEnvironmentBillboardRecord(DrawRecord) == true ? DrawRecord.mPipeline : Context.mPipelineProvider->ResolveEnvironmentObjectPipeline() };
            if (Pipeline == nullptr) {
                continue;
            }

            ActivePipeline = Pipeline->Set(ActivePipeline, Context.mCommandList);

            DrawRootConstantsB1 RootConstants{};
            RootConstants.mFrameGlobalsSrvIndex = Context.mFrameGlobalsSrvIndex;
            RootConstants.mModelContextSrvIndex = GpuFrame.mInstanceContextSrvIndex;
            RootConstants.mBonePaletteSrvIndex = GpuFrame.mSegmentContextSrvIndex;
            RootConstants.mDrawRecordSrvIndex = GpuFrame.mDrawRecordSrvIndex;
            RootConstants.mDrawRecordBaseIndex = static_cast<std::uint32_t>(DrawRecordIndex);
            RootConstants.mMaterialSrvIndex = Context.mMaterialSrvIndex;
            RootConstants.mMaterialTextureTableSrvIndex = Context.mMaterialTextureTableSrvIndex;
            RootConstants.mShadowMappingParameterSrvIndex = InvalidDescriptorIndex;
            RootConstants.mShadowMapTextureBaseSrvIndex = InvalidDescriptorIndex;
            RootConstants.mFrameGlobalsElementIndex = 0u;
            RootConstants.mTerrainPatchContextSrvIndex = InvalidDescriptorIndex;
            RootConstants.mReserved1 = GpuFrame.mVisibleInstanceIndexSrvIndex;
            Context.mCommandList->SetGraphicsRoot32BitConstants(0, sizeof(DrawRootConstantsB1) / sizeof(std::uint32_t), &RootConstants, 0);

            Context.mCommandList->IASetPrimitiveTopology(Pipeline->GetPrimitiveTopology());

            const std::vector<D3D12_VERTEX_BUFFER_VIEW>& VertexBufferViews{ ResolveVertexBufferViews(*Pipeline, *DrawRecord.mMesh) };
            if (VertexBufferViews.empty() == false) {
                Context.mCommandList->IASetVertexBuffers(0, static_cast<UINT>(VertexBufferViews.size()), VertexBufferViews.data());
            }

            const D3D12_INDEX_BUFFER_VIEW& IndexBufferView{ DrawRecord.mMesh->GetIndexBufferView() };
            Context.mCommandList->IASetIndexBuffer(&IndexBufferView);

            const std::uint64_t IndirectArgumentOffset{ sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) * DrawRecordIndex };
            Context.mCommandList->ExecuteIndirect(mDrawIndexedIndirectCommandSignature.Get(), 1u, GpuFrame.mIndirectArgumentResource, IndirectArgumentOffset, nullptr, 0u);
        }

        std::array<D3D12_RESOURCE_BARRIER, 3> RestoreBarriers{};
        RestoreBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        RestoreBarriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        RestoreBarriers[0].Transition.pResource = GpuFrame.mInstanceContextResource;
        RestoreBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        RestoreBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        RestoreBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        RestoreBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        RestoreBarriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        RestoreBarriers[1].Transition.pResource = GpuFrame.mVisibleInstanceIndexResource;
        RestoreBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        RestoreBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        RestoreBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        RestoreBarriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        RestoreBarriers[2].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        RestoreBarriers[2].Transition.pResource = GpuFrame.mIndirectArgumentResource;
        RestoreBarriers[2].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        RestoreBarriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        RestoreBarriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        Context.mCommandList->ResourceBarrier(static_cast<UINT>(RestoreBarriers.size()), RestoreBarriers.data());
    }

    void EnvironmentRuntime::RecordShadowDepthIndirect(const RenderContract::EnvironmentShadowDepthRenderCommandContext& Context) {
        if (Context.mRenderFrameData == nullptr || Context.mPipelineProvider == nullptr) {
            return;
        }

        const RenderContract::EnvironmentGpuDrivenFrameData& GpuFrame{ Context.mRenderFrameData->mEnvironmentGpuDrivenFrame };
        if (GpuFrame.mEnabled == false || GpuFrame.mInstanceContextResource == nullptr || GpuFrame.mVisibleInstanceIndexResource == nullptr || GpuFrame.mIndirectArgumentResource == nullptr || EnsureDrawIndexedIndirectCommandSignature() == false) {
            return;
        }

        std::array<D3D12_RESOURCE_BARRIER, 3> Barriers{};
        Barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        Barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        Barriers[0].Transition.pResource = GpuFrame.mInstanceContextResource;
        Barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        Barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        Barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        Barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        Barriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        Barriers[1].Transition.pResource = GpuFrame.mVisibleInstanceIndexResource;
        Barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        Barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        Barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        Barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        Barriers[2].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        Barriers[2].Transition.pResource = GpuFrame.mIndirectArgumentResource;
        Barriers[2].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        Barriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        Barriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        Context.mCommandList->ResourceBarrier(static_cast<UINT>(Barriers.size()), Barriers.data());

        const RenderContract::IPipeline* ActivePipeline{ nullptr };
        const std::size_t DrawRecordCount{ std::min<std::size_t>(Context.mRenderFrameData->mEnvironmentDrawRecords.size(), GpuFrame.mDrawRecordCount) };
        for (std::size_t DrawRecordIndex{}; DrawRecordIndex < DrawRecordCount; DrawRecordIndex += 1ULL) {
            const RenderContract::EnvironmentDrawRecord& DrawRecord{ Context.mRenderFrameData->mEnvironmentDrawRecords[DrawRecordIndex] };
            if (DrawRecord.mMesh == nullptr || DrawRecord.mInstanceCount == 0u || DrawRecord.mCastsShadow == false) {
                continue;
            }

            const RenderContract::IPipeline* Pipeline{ IsEnvironmentBillboardRecord(DrawRecord) == true ? Context.mPipelineProvider->ResolveEnvironmentBillboardDepthPipeline() : Context.mPipelineProvider->ResolveEnvironmentObjectDepthPipeline() };
            if (Pipeline == nullptr) {
                continue;
            }

            ActivePipeline = Pipeline->Set(ActivePipeline, Context.mCommandList);
            if (Context.mDynamicDepthBiasCommandList != nullptr) {
                Context.mDynamicDepthBiasCommandList->RSSetDepthBias(Context.mRasterDepthBias, Context.mRasterDepthBiasClamp, Context.mRasterSlopeScaledDepthBias);
            }

            DrawRootConstantsB1 RootConstants{};
            RootConstants.mFrameGlobalsSrvIndex = Context.mFrameGlobalsSrvIndex;
            RootConstants.mModelContextSrvIndex = GpuFrame.mInstanceContextSrvIndex;
            RootConstants.mBonePaletteSrvIndex = GpuFrame.mSegmentContextSrvIndex;
            RootConstants.mDrawRecordSrvIndex = GpuFrame.mDrawRecordSrvIndex;
            RootConstants.mDrawRecordBaseIndex = static_cast<std::uint32_t>(DrawRecordIndex);
            RootConstants.mMaterialSrvIndex = Context.mMaterialSrvIndex;
            RootConstants.mMaterialTextureTableSrvIndex = Context.mMaterialTextureTableSrvIndex;
            RootConstants.mShadowMappingParameterSrvIndex = InvalidDescriptorIndex;
            RootConstants.mShadowMapTextureBaseSrvIndex = InvalidDescriptorIndex;
            RootConstants.mFrameGlobalsElementIndex = Context.mShadowFrameGlobalsIndex;
            RootConstants.mTerrainPatchContextSrvIndex = InvalidDescriptorIndex;
            RootConstants.mReserved1 = GpuFrame.mVisibleInstanceIndexSrvIndex;
            Context.mCommandList->SetGraphicsRoot32BitConstants(0, sizeof(DrawRootConstantsB1) / sizeof(std::uint32_t), &RootConstants, 0);

            Context.mCommandList->IASetPrimitiveTopology(Pipeline->GetPrimitiveTopology());

            const std::vector<D3D12_VERTEX_BUFFER_VIEW>& VertexBufferViews{ ResolveVertexBufferViews(*Pipeline, *DrawRecord.mMesh) };
            if (VertexBufferViews.empty() == false) {
                Context.mCommandList->IASetVertexBuffers(0, static_cast<UINT>(VertexBufferViews.size()), VertexBufferViews.data());
            }

            const D3D12_INDEX_BUFFER_VIEW& IndexBufferView{ DrawRecord.mMesh->GetIndexBufferView() };
            Context.mCommandList->IASetIndexBuffer(&IndexBufferView);

            const std::uint64_t IndirectArgumentOffset{ sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) * DrawRecordIndex };
            Context.mCommandList->ExecuteIndirect(mDrawIndexedIndirectCommandSignature.Get(), 1u, GpuFrame.mIndirectArgumentResource, IndirectArgumentOffset, nullptr, 0u);
        }

        std::array<D3D12_RESOURCE_BARRIER, 3> RestoreBarriers{};
        RestoreBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        RestoreBarriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        RestoreBarriers[0].Transition.pResource = GpuFrame.mInstanceContextResource;
        RestoreBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        RestoreBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        RestoreBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        RestoreBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        RestoreBarriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        RestoreBarriers[1].Transition.pResource = GpuFrame.mVisibleInstanceIndexResource;
        RestoreBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        RestoreBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        RestoreBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        RestoreBarriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        RestoreBarriers[2].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        RestoreBarriers[2].Transition.pResource = GpuFrame.mIndirectArgumentResource;
        RestoreBarriers[2].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        RestoreBarriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        RestoreBarriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        Context.mCommandList->ResourceBarrier(static_cast<UINT>(RestoreBarriers.size()), RestoreBarriers.data());
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

        if (CreateEnvironmentComputePipelineState(mDevice, mComputeRootSignature.Get(), "cs_6_6:GenerateCandidatesCsMain", mCandidateGeneratePipelineState) == false) {
            return false;
        }

        if (CreateEnvironmentComputePipelineState(mDevice, mComputeRootSignature.Get(), "cs_6_6:ClassifyCandidatesCsMain", mCandidateClassifyPipelineState) == false) {
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

        if (FrameResource.mInstanceContextUavHandle.IsValid() == false) {
            FrameResource.mInstanceContextUavHandle = mSrvHeap->Allocate();
        }

        if (FrameResource.mSegmentContextSrvHandle.IsValid() == false) {
            FrameResource.mSegmentContextSrvHandle = mSrvHeap->Allocate();
        }

        if (FrameResource.mDrawRecordSrvHandle.IsValid() == false) {
            FrameResource.mDrawRecordSrvHandle = mSrvHeap->Allocate();
        }

        if (FrameResource.mPlacementConfigSrvHandle.IsValid() == false) {
            FrameResource.mPlacementConfigSrvHandle = mSrvHeap->Allocate();
        }

        if (FrameResource.mPlacementRuleSrvHandle.IsValid() == false) {
            FrameResource.mPlacementRuleSrvHandle = mSrvHeap->Allocate();
        }

        if (FrameResource.mPlacementDrawRecordSrvHandle.IsValid() == false) {
            FrameResource.mPlacementDrawRecordSrvHandle = mSrvHeap->Allocate();
        }

        if (FrameResource.mPlacementCandidateRecordSrvHandle.IsValid() == false) {
            FrameResource.mPlacementCandidateRecordSrvHandle = mSrvHeap->Allocate();
        }

        if (FrameResource.mCandidateContextSrvHandle.IsValid() == false) {
            FrameResource.mCandidateContextSrvHandle = mSrvHeap->Allocate();
        }

        if (FrameResource.mCandidateContextUavHandle.IsValid() == false) {
            FrameResource.mCandidateContextUavHandle = mSrvHeap->Allocate();
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

        return FrameResource.mInstanceContextSrvHandle.IsValid() == true && FrameResource.mInstanceContextUavHandle.IsValid() == true && FrameResource.mSegmentContextSrvHandle.IsValid() == true && FrameResource.mDrawRecordSrvHandle.IsValid() == true && FrameResource.mPlacementConfigSrvHandle.IsValid() == true && FrameResource.mPlacementRuleSrvHandle.IsValid() == true && FrameResource.mPlacementDrawRecordSrvHandle.IsValid() == true && FrameResource.mPlacementCandidateRecordSrvHandle.IsValid() == true && FrameResource.mCandidateContextSrvHandle.IsValid() == true && FrameResource.mCandidateContextUavHandle.IsValid() == true && FrameResource.mVisibleInstanceIndexSrvHandle.IsValid() == true && FrameResource.mVisibleInstanceIndexUavHandle.IsValid() == true && FrameResource.mIndirectArgumentUavHandle.IsValid() == true;
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

    bool EnvironmentRuntime::EnsureGpuDrivenFrameResources(EnvironmentGpuDrivenFrameResource& FrameResource, std::uint32_t InstanceContextCount, std::uint32_t SegmentContextCount, std::uint32_t DrawRecordCount, std::uint32_t PlacementConfigCount, std::uint32_t PlacementRuleCount, std::uint32_t PlacementDrawRecordCount, std::uint32_t PlacementCandidateRecordCount, std::uint32_t CandidateContextCount, std::uint32_t VisibleInstanceIndexCount) {
        if (EnsureGpuDrivenDescriptorHandles(FrameResource) == false) {
            return false;
        }

        const std::uint32_t SafeInstanceContextCount{ std::max(InstanceContextCount, 1u) };
        const std::uint32_t SafeSegmentContextCount{ std::max(SegmentContextCount, 1u) };
        const std::uint32_t SafeDrawRecordCount{ std::max(DrawRecordCount, 1u) };
        const std::uint32_t SafePlacementConfigCount{ std::max(PlacementConfigCount, 1u) };
        const std::uint32_t SafePlacementRuleCount{ std::max(PlacementRuleCount, 1u) };
        const std::uint32_t SafePlacementDrawRecordCount{ std::max(PlacementDrawRecordCount, 1u) };
        const std::uint32_t SafePlacementCandidateRecordCount{ std::max(PlacementCandidateRecordCount, 1u) };
        const std::uint32_t SafeCandidateContextCount{ std::max(CandidateContextCount, 1u) };
        const std::uint32_t SafeVisibleInstanceIndexCount{ std::max(VisibleInstanceIndexCount, 1u) };
        ID3D12Resource* PreviousInstanceContextBuffer{ FrameResource.mInstanceContextBuffer.Get() };
        ID3D12Resource* PreviousCandidateContextBuffer{ FrameResource.mCandidateContextBuffer.Get() };
        ID3D12Resource* PreviousVisibleInstanceIndexBuffer{ FrameResource.mVisibleInstanceIndexBuffer.Get() };
        ID3D12Resource* PreviousIndirectArgumentBuffer{ FrameResource.mIndirectArgumentBuffer.Get() };

        bool Result{ true };
        Result = EnsureGpuDrivenBuffer(FrameResource.mInstanceContextBuffer, FrameResource.mInstanceContextBufferCapacityInBytes, sizeof(RenderContract::EnvironmentInstanceContext) * static_cast<std::uint64_t>(SafeInstanceContextCount), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.InstanceContextBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(FrameResource.mSegmentContextBuffer, FrameResource.mSegmentContextBufferCapacityInBytes, sizeof(RenderContract::EnvironmentSegmentContext) * static_cast<std::uint64_t>(SafeSegmentContextCount), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.SegmentContextBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(FrameResource.mDrawRecordBuffer, FrameResource.mDrawRecordBufferCapacityInBytes, sizeof(RenderContract::EnvironmentDrawRecordGpu) * static_cast<std::uint64_t>(SafeDrawRecordCount), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.DrawRecordBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(FrameResource.mPlacementConfigBuffer, FrameResource.mPlacementConfigBufferCapacityInBytes, sizeof(EnvironmentGpuPlacementConfig) * static_cast<std::uint64_t>(SafePlacementConfigCount), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.PlacementConfigBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(FrameResource.mPlacementRuleBuffer, FrameResource.mPlacementRuleBufferCapacityInBytes, sizeof(EnvironmentGpuPlacementRule) * static_cast<std::uint64_t>(SafePlacementRuleCount), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.PlacementRuleBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(FrameResource.mPlacementDrawRecordBuffer, FrameResource.mPlacementDrawRecordBufferCapacityInBytes, sizeof(EnvironmentGpuPlacementDrawRecord) * static_cast<std::uint64_t>(SafePlacementDrawRecordCount), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.PlacementDrawRecordBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(FrameResource.mPlacementCandidateRecordBuffer, FrameResource.mPlacementCandidateRecordBufferCapacityInBytes, sizeof(EnvironmentGpuPlacementCandidateRecord) * static_cast<std::uint64_t>(SafePlacementCandidateRecordCount), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.PlacementCandidateRecordBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(FrameResource.mCandidateContextBuffer, FrameResource.mCandidateContextBufferCapacityInBytes, sizeof(EnvironmentGpuPlacementCandidate) * static_cast<std::uint64_t>(SafeCandidateContextCount), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.CandidateContextBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(FrameResource.mVisibleInstanceIndexBuffer, FrameResource.mVisibleInstanceIndexBufferCapacityInBytes, sizeof(std::uint32_t) * static_cast<std::uint64_t>(SafeVisibleInstanceIndexCount), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.VisibleInstanceIndexBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(FrameResource.mIndirectArgumentBuffer, FrameResource.mIndirectArgumentBufferCapacityInBytes, sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) * static_cast<std::uint64_t>(SafeDrawRecordCount), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.IndirectArgumentBuffer") && Result;
        if (FrameResource.mInstanceContextBuffer.Get() != PreviousInstanceContextBuffer) {
            FrameResource.mInstanceContextState = D3D12_RESOURCE_STATE_COMMON;
        }

        if (FrameResource.mCandidateContextBuffer.Get() != PreviousCandidateContextBuffer) {
            FrameResource.mCandidateContextState = D3D12_RESOURCE_STATE_COMMON;
        }

        if (FrameResource.mVisibleInstanceIndexBuffer.Get() != PreviousVisibleInstanceIndexBuffer) {
            FrameResource.mVisibleInstanceIndexState = D3D12_RESOURCE_STATE_COMMON;
        }

        if (FrameResource.mIndirectArgumentBuffer.Get() != PreviousIndirectArgumentBuffer) {
            FrameResource.mIndirectArgumentState = D3D12_RESOURCE_STATE_COMMON;
        }

        return Result;
    }

    void EnvironmentRuntime::BuildGpuDrivenFrameData(const EnvironmentFrameInput& Input, RenderContract::RenderFrameData& RenderData, std::uint32_t& OutVisibleInstanceIndexCount) {
        mGpuPlacementFrameData.mConfig.mTerrainPosition = SimpleMath::Vector4{ Input.mTerrain.mPosition.x, Input.mTerrain.mPosition.y, Input.mTerrain.mPosition.z, Input.mTerrain.mMaxHeight };
        mGpuPlacementFrameData.mConfig.mTerrainScale = SimpleMath::Vector4{ Input.mTerrain.mScale.x, Input.mTerrain.mScale.y, Input.mTerrain.mScale.z, 0.0f };
        mGpuPlacementFrameData.mConfig.mTerrainGridParameters = SimpleMath::Vector4{ Input.mTerrain.mCellSizeX, Input.mTerrain.mCellSizeZ, Input.mTerrain.mOriginOffsetX, Input.mTerrain.mOriginOffsetZ };
        mGpuPlacementFrameData.mConfig.mTerrainSizeParameters = SimpleMath::Vector4{ static_cast<float>(Input.mTerrain.mWidth), static_cast<float>(Input.mTerrain.mHeight), static_cast<float>(Input.mTerrain.mSplatWidth), static_cast<float>(Input.mTerrain.mSplatHeight) };
        mGpuPlacementFrameData.mConfig.mTerrainSeed = Input.mTerrain.mSeed;
        std::vector<EnvironmentGpuDrawBuildItem> DrawBuildItems{};
        const std::size_t DrawBuildItemCount{ std::min(RenderData.mEnvironmentDrawRecords.size(), mGpuPlacementFrameData.mDrawRecords.size()) };
        DrawBuildItems.reserve(DrawBuildItemCount);
        for (std::size_t DrawRecordIndex{}; DrawRecordIndex < DrawBuildItemCount; DrawRecordIndex += 1ULL) {
            EnvironmentGpuDrawBuildItem Item{};
            Item.mDrawRecord = RenderData.mEnvironmentDrawRecords[DrawRecordIndex];
            Item.mPlacementDrawRecord = mGpuPlacementFrameData.mDrawRecords[DrawRecordIndex];
            DrawBuildItems.push_back(std::move(Item));
        }

        std::sort(DrawBuildItems.begin(), DrawBuildItems.end(), [](const EnvironmentGpuDrawBuildItem& Left, const EnvironmentGpuDrawBuildItem& Right) {
            return CompareEnvironmentDrawRecordByPso(Left.mDrawRecord, Right.mDrawRecord);
        });

        RenderData.mEnvironmentDrawRecords.clear();
        RenderData.mEnvironmentDrawRecords.reserve(DrawBuildItems.size());
        mGpuPlacementFrameData.mDrawRecords.clear();
        mGpuPlacementFrameData.mDrawRecords.reserve(DrawBuildItems.size());
        for (const EnvironmentGpuDrawBuildItem& Item : DrawBuildItems) {
            RenderData.mEnvironmentDrawRecords.push_back(Item.mDrawRecord);
            mGpuPlacementFrameData.mDrawRecords.push_back(Item.mPlacementDrawRecord);
        }

        mGpuInstanceContexts.clear();
        mGpuInstanceContextCount = 0u;
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
            const std::uint64_t InstanceEnd{ static_cast<std::uint64_t>(SourceRecord.mInstanceOffset) + static_cast<std::uint64_t>(SourceRecord.mInstanceCount) };
            mGpuInstanceContextCount = static_cast<std::uint32_t>(std::min<std::uint64_t>(std::max<std::uint64_t>(mGpuInstanceContextCount, InstanceEnd), std::numeric_limits<std::uint32_t>::max()));

            D3D12_DRAW_INDEXED_ARGUMENTS& DrawArguments{ mGpuIndirectArguments[DrawRecordIndex] };
            if (SourceRecord.mMesh != nullptr && SourceRecord.mInstanceCount > 0u) {
                const RenderContract::ModelSubMesh& SubMesh{ SourceRecord.mMesh->GetSubMesh(SourceRecord.mSubMesh) };
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
        if (mCopyQueue == nullptr || FrameResource.mSegmentContextBuffer == nullptr || FrameResource.mDrawRecordBuffer == nullptr || FrameResource.mPlacementConfigBuffer == nullptr || FrameResource.mPlacementRuleBuffer == nullptr || FrameResource.mPlacementDrawRecordBuffer == nullptr || FrameResource.mPlacementCandidateRecordBuffer == nullptr || FrameResource.mIndirectArgumentBuffer == nullptr) {
            return RenderContract::Future{};
        }

        std::vector<Interface::CopyRequest> CopyRequests{};
        CopyRequests.reserve(7ULL);

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

        Interface::CopyRequest PlacementConfigCopyRequest{ Interface::CopyPriority::High };
        PlacementConfigCopyRequest.DestinationDefaultResource = FrameResource.mPlacementConfigBuffer;
        PlacementConfigCopyRequest.DestinationOffset = 0u;
        PlacementConfigCopyRequest.SourceData = MakeByteSpan(&mGpuPlacementFrameData.mConfig, sizeof(EnvironmentGpuPlacementConfig));
        CopyRequests.push_back(PlacementConfigCopyRequest);

        Interface::CopyRequest PlacementRuleCopyRequest{ Interface::CopyPriority::High };
        PlacementRuleCopyRequest.DestinationDefaultResource = FrameResource.mPlacementRuleBuffer;
        PlacementRuleCopyRequest.DestinationOffset = 0u;
        PlacementRuleCopyRequest.SourceData = MakeVectorByteSpan(mGpuPlacementFrameData.mRules);
        CopyRequests.push_back(PlacementRuleCopyRequest);

        Interface::CopyRequest PlacementDrawRecordCopyRequest{ Interface::CopyPriority::High };
        PlacementDrawRecordCopyRequest.DestinationDefaultResource = FrameResource.mPlacementDrawRecordBuffer;
        PlacementDrawRecordCopyRequest.DestinationOffset = 0u;
        PlacementDrawRecordCopyRequest.SourceData = MakeVectorByteSpan(mGpuPlacementFrameData.mDrawRecords);
        CopyRequests.push_back(PlacementDrawRecordCopyRequest);

        Interface::CopyRequest PlacementCandidateRecordCopyRequest{ Interface::CopyPriority::High };
        PlacementCandidateRecordCopyRequest.DestinationDefaultResource = FrameResource.mPlacementCandidateRecordBuffer;
        PlacementCandidateRecordCopyRequest.DestinationOffset = 0u;
        PlacementCandidateRecordCopyRequest.SourceData = MakeVectorByteSpan(mGpuPlacementFrameData.mCandidateRecords);
        CopyRequests.push_back(PlacementCandidateRecordCopyRequest);

        Interface::CopyRequest IndirectArgumentCopyRequest{ Interface::CopyPriority::High };
        IndirectArgumentCopyRequest.DestinationDefaultResource = FrameResource.mIndirectArgumentBuffer;
        IndirectArgumentCopyRequest.DestinationOffset = 0u;
        IndirectArgumentCopyRequest.SourceData = MakeByteSpan(mGpuIndirectArguments.data(), sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) * mGpuIndirectArguments.size());
        CopyRequests.push_back(IndirectArgumentCopyRequest);

        RenderContract::Future CopyFuture{ mCopyQueue->EnqueueCopyFuture(CopyRequests) };
        mCopyQueue->DispatchCopies();
        return CopyFuture;
    }

    RenderContract::Future EnvironmentRuntime::DispatchGpuDrivenFrame(EnvironmentGpuDrivenFrameResource& FrameResource, const EnvironmentFrameInput& Input, const RenderContract::Future& CopyFuture, std::uint32_t DrawRecordCount, std::uint32_t VisibleInstanceIndexCapacity, std::uint32_t CandidateRecordCount) {
        if (mComputeQueue == nullptr || mSrvHeap == nullptr || DrawRecordCount == 0u || CandidateRecordCount == 0u || mGpuStatusUavIndex == InvalidDescriptorIndex || mCandidateGeneratePipelineState == nullptr || mCandidateClassifyPipelineState == nullptr || FrameResource.mInstanceContextSrvHandle.IsValid() == false || FrameResource.mInstanceContextUavHandle.IsValid() == false || FrameResource.mDrawRecordSrvHandle.IsValid() == false || FrameResource.mPlacementConfigSrvHandle.IsValid() == false || FrameResource.mPlacementRuleSrvHandle.IsValid() == false || FrameResource.mPlacementDrawRecordSrvHandle.IsValid() == false || FrameResource.mPlacementCandidateRecordSrvHandle.IsValid() == false || FrameResource.mCandidateContextSrvHandle.IsValid() == false || FrameResource.mCandidateContextUavHandle.IsValid() == false || FrameResource.mIndirectArgumentUavHandle.IsValid() == false || FrameResource.mVisibleInstanceIndexUavHandle.IsValid() == false) {
            return RenderContract::Future{};
        }

        Microsoft::WRL::ComPtr<ID3D12Resource> InstanceContextBuffer{ FrameResource.mInstanceContextBuffer };
        Microsoft::WRL::ComPtr<ID3D12Resource> CandidateContextBuffer{ FrameResource.mCandidateContextBuffer };
        Microsoft::WRL::ComPtr<ID3D12Resource> VisibleInstanceIndexBuffer{ FrameResource.mVisibleInstanceIndexBuffer };
        Microsoft::WRL::ComPtr<ID3D12Resource> IndirectArgumentBuffer{ FrameResource.mIndirectArgumentBuffer };
        const D3D12_RESOURCE_STATES InstanceContextStateBeforeDispatch{ FrameResource.mInstanceContextState };
        const D3D12_RESOURCE_STATES CandidateContextStateBeforeDispatch{ FrameResource.mCandidateContextState };
        const D3D12_RESOURCE_STATES VisibleInstanceIndexStateBeforeDispatch{ FrameResource.mVisibleInstanceIndexState };
        const D3D12_RESOURCE_STATES IndirectArgumentStateBeforeDispatch{ FrameResource.mIndirectArgumentState };
        const EnvironmentGpuRootConstants RootConstants{ BuildEnvironmentGpuRootConstants(Input, mGpuStatusUavIndex, FrameResource.mInstanceContextSrvHandle.GetIndex(), FrameResource.mInstanceContextUavHandle.GetIndex(), FrameResource.mDrawRecordSrvHandle.GetIndex(), FrameResource.mPlacementConfigSrvHandle.GetIndex(), FrameResource.mPlacementRuleSrvHandle.GetIndex(), FrameResource.mPlacementDrawRecordSrvHandle.GetIndex(), FrameResource.mPlacementCandidateRecordSrvHandle.GetIndex(), FrameResource.mCandidateContextSrvHandle.GetIndex(), FrameResource.mCandidateContextUavHandle.GetIndex(), FrameResource.mIndirectArgumentUavHandle.GetIndex(), FrameResource.mVisibleInstanceIndexUavHandle.GetIndex(), DrawRecordCount, VisibleInstanceIndexCapacity, CandidateRecordCount) };

        const std::array<RenderContract::Future, 2> WaitFutures{ CopyFuture, Input.mTerrain.mUploadFuture };
        Interface::ComputeQueueDispatchRequest GenerateRequest{};
        GenerateRequest.WaitFuture = RenderContract::Future::Merge(WaitFutures);
        GenerateRequest.RootSignature = mComputeRootSignature;
        GenerateRequest.PipelineState = mCandidateGeneratePipelineState;
        GenerateRequest.DescriptorHeaps = std::vector<ID3D12DescriptorHeap*>{ mSrvHeap->GetHeap() };
        GenerateRequest.RecordCommands = [CandidateContextBuffer, RootConstants, CandidateContextStateBeforeDispatch](ID3D12GraphicsCommandList* CommandList) {
            if (CommandList == nullptr) {
                return;
            }

            if (CandidateContextBuffer != nullptr && CandidateContextStateBeforeDispatch != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
                D3D12_RESOURCE_BARRIER Barrier{};
                Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                Barrier.Transition.pResource = CandidateContextBuffer.Get();
                Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                Barrier.Transition.StateBefore = CandidateContextStateBeforeDispatch;
                Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                CommandList->ResourceBarrier(1u, &Barrier);
            }

            CommandList->SetComputeRoot32BitConstants(0, EnvironmentGpuRootConstantDwordCount, &RootConstants, 0);
        };
        GenerateRequest.ThreadGroupCountX = CandidateRecordCount;
        GenerateRequest.ThreadGroupCountY = 1u;
        GenerateRequest.ThreadGroupCountZ = 1u;

        Interface::ComputeQueueDispatchRequest ClassifyRequest{};
        ClassifyRequest.RootSignature = mComputeRootSignature;
        ClassifyRequest.PipelineState = mCandidateClassifyPipelineState;
        ClassifyRequest.DescriptorHeaps = std::vector<ID3D12DescriptorHeap*>{ mSrvHeap->GetHeap() };
        ClassifyRequest.RecordCommands = [InstanceContextBuffer, CandidateContextBuffer, VisibleInstanceIndexBuffer, IndirectArgumentBuffer, RootConstants, InstanceContextStateBeforeDispatch, VisibleInstanceIndexStateBeforeDispatch, IndirectArgumentStateBeforeDispatch](ID3D12GraphicsCommandList* CommandList) {
            if (CommandList == nullptr) {
                return;
            }

            std::array<D3D12_RESOURCE_BARRIER, 4> Barriers{};
            std::uint32_t BarrierCount{};
            if (CandidateContextBuffer != nullptr) {
                D3D12_RESOURCE_BARRIER& Barrier{ Barriers[BarrierCount] };
                Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                Barrier.Transition.pResource = CandidateContextBuffer.Get();
                Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                BarrierCount += 1u;
            }

            if (InstanceContextBuffer != nullptr && InstanceContextStateBeforeDispatch != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
                D3D12_RESOURCE_BARRIER& Barrier{ Barriers[BarrierCount] };
                Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                Barrier.Transition.pResource = InstanceContextBuffer.Get();
                Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                Barrier.Transition.StateBefore = InstanceContextStateBeforeDispatch;
                Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                BarrierCount += 1u;
            }

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
        ClassifyRequest.ThreadGroupCountX = DrawRecordCount;
        ClassifyRequest.ThreadGroupCountY = 1u;
        ClassifyRequest.ThreadGroupCountZ = 1u;

        const std::array<Interface::ComputeQueueDispatchRequest, 2> DispatchRequests{ GenerateRequest, ClassifyRequest };
        RenderContract::Future GpuFuture{ mComputeQueue->EnqueueComputeFuture(std::span<const Interface::ComputeQueueDispatchRequest>{ DispatchRequests.data(), DispatchRequests.size() }) };
        mComputeQueue->DispatchComputes();
        FrameResource.mInstanceContextState = D3D12_RESOURCE_STATE_COMMON;
        FrameResource.mCandidateContextState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
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
        Payload.mInstanceContextCount = mGpuInstanceContextCount;
        Payload.mEnabled = GpuDispatchFuture.IsValid() == true && Payload.mInstanceContextResource != nullptr && Payload.mSegmentContextResource != nullptr && Payload.mDrawRecordResource != nullptr && Payload.mVisibleInstanceIndexResource != nullptr && Payload.mIndirectArgumentResource != nullptr;
        RenderData.mEnvironmentGpuDrivenFrame = Payload;
    }

    void EnvironmentRuntime::UpdateGpuDrivenShaderResourceViews(EnvironmentGpuDrivenFrameResource& FrameResource, std::uint32_t InstanceContextCount, std::uint32_t SegmentContextCount, std::uint32_t DrawRecordCount, std::uint32_t PlacementConfigCount, std::uint32_t PlacementRuleCount, std::uint32_t PlacementDrawRecordCount, std::uint32_t PlacementCandidateRecordCount, std::uint32_t CandidateContextCount, std::uint32_t VisibleInstanceIndexCount) {
        if (mDevice == nullptr) {
            return;
        }

        const std::uint32_t SafeInstanceContextCount{ std::max(InstanceContextCount, 1u) };
        const std::uint32_t SafeSegmentContextCount{ std::max(SegmentContextCount, 1u) };
        const std::uint32_t SafeDrawRecordCount{ std::max(DrawRecordCount, 1u) };
        const std::uint32_t SafePlacementConfigCount{ std::max(PlacementConfigCount, 1u) };
        const std::uint32_t SafePlacementRuleCount{ std::max(PlacementRuleCount, 1u) };
        const std::uint32_t SafePlacementDrawRecordCount{ std::max(PlacementDrawRecordCount, 1u) };
        const std::uint32_t SafePlacementCandidateRecordCount{ std::max(PlacementCandidateRecordCount, 1u) };
        const std::uint32_t SafeCandidateContextCount{ std::max(CandidateContextCount, 1u) };
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

        SrvDesc.Buffer.NumElements = SafePlacementConfigCount;
        SrvDesc.Buffer.StructureByteStride = sizeof(EnvironmentGpuPlacementConfig);
        mDevice->CreateShaderResourceView(FrameResource.mPlacementConfigBuffer.Get(), &SrvDesc, FrameResource.mPlacementConfigSrvHandle.GetCPU());

        SrvDesc.Buffer.NumElements = SafePlacementRuleCount;
        SrvDesc.Buffer.StructureByteStride = sizeof(EnvironmentGpuPlacementRule);
        mDevice->CreateShaderResourceView(FrameResource.mPlacementRuleBuffer.Get(), &SrvDesc, FrameResource.mPlacementRuleSrvHandle.GetCPU());

        SrvDesc.Buffer.NumElements = SafePlacementDrawRecordCount;
        SrvDesc.Buffer.StructureByteStride = sizeof(EnvironmentGpuPlacementDrawRecord);
        mDevice->CreateShaderResourceView(FrameResource.mPlacementDrawRecordBuffer.Get(), &SrvDesc, FrameResource.mPlacementDrawRecordSrvHandle.GetCPU());

        SrvDesc.Buffer.NumElements = SafePlacementCandidateRecordCount;
        SrvDesc.Buffer.StructureByteStride = sizeof(EnvironmentGpuPlacementCandidateRecord);
        mDevice->CreateShaderResourceView(FrameResource.mPlacementCandidateRecordBuffer.Get(), &SrvDesc, FrameResource.mPlacementCandidateRecordSrvHandle.GetCPU());

        SrvDesc.Buffer.NumElements = SafeCandidateContextCount;
        SrvDesc.Buffer.StructureByteStride = sizeof(EnvironmentGpuPlacementCandidate);
        mDevice->CreateShaderResourceView(FrameResource.mCandidateContextBuffer.Get(), &SrvDesc, FrameResource.mCandidateContextSrvHandle.GetCPU());

        SrvDesc.Buffer.NumElements = SafeVisibleInstanceIndexCount;
        SrvDesc.Buffer.StructureByteStride = sizeof(std::uint32_t);
        mDevice->CreateShaderResourceView(FrameResource.mVisibleInstanceIndexBuffer.Get(), &SrvDesc, FrameResource.mVisibleInstanceIndexSrvHandle.GetCPU());
    }

    void EnvironmentRuntime::UpdateGpuDrivenUnorderedAccessViews(EnvironmentGpuDrivenFrameResource& FrameResource, std::uint32_t InstanceContextCount, std::uint32_t CandidateContextCount, std::uint32_t VisibleInstanceIndexCount, std::uint32_t IndirectArgumentCount) {
        if (mDevice == nullptr) {
            return;
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC UavDesc{};
        UavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        UavDesc.Format = DXGI_FORMAT_UNKNOWN;
        UavDesc.Buffer.FirstElement = 0u;
        UavDesc.Buffer.CounterOffsetInBytes = 0u;
        UavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

        UavDesc.Buffer.NumElements = std::max(InstanceContextCount, 1u);
        UavDesc.Buffer.StructureByteStride = sizeof(RenderContract::EnvironmentInstanceContext);
        mDevice->CreateUnorderedAccessView(FrameResource.mInstanceContextBuffer.Get(), nullptr, &UavDesc, FrameResource.mInstanceContextUavHandle.GetCPU());

        UavDesc.Buffer.NumElements = std::max(CandidateContextCount, 1u);
        UavDesc.Buffer.StructureByteStride = sizeof(EnvironmentGpuPlacementCandidate);
        mDevice->CreateUnorderedAccessView(FrameResource.mCandidateContextBuffer.Get(), nullptr, &UavDesc, FrameResource.mCandidateContextUavHandle.GetCPU());

        UavDesc.Buffer.NumElements = std::max(VisibleInstanceIndexCount, 1u);
        UavDesc.Buffer.StructureByteStride = sizeof(std::uint32_t);
        mDevice->CreateUnorderedAccessView(FrameResource.mVisibleInstanceIndexBuffer.Get(), nullptr, &UavDesc, FrameResource.mVisibleInstanceIndexUavHandle.GetCPU());

        UavDesc.Buffer.NumElements = std::max(IndirectArgumentCount, 1u);
        UavDesc.Buffer.StructureByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        mDevice->CreateUnorderedAccessView(FrameResource.mIndirectArgumentBuffer.Get(), nullptr, &UavDesc, FrameResource.mIndirectArgumentUavHandle.GetCPU());
    }

    void EnvironmentRuntime::ResetGpuResources() {
        mComputeRootSignature.Reset();
        mCandidateGeneratePipelineState.Reset();
        mCandidateClassifyPipelineState.Reset();
        mDrawIndexedIndirectCommandSignature.Reset();
        mGpuStatusBuffer.Reset();
        mGpuStatusUavHandle = Core::DX::DescriptorHandle{};
        mGpuInstanceContexts.clear();
        mGpuSegmentContexts.clear();
        mGpuDrawRecords.clear();
        mGpuIndirectArguments.clear();
        mGpuPlacementFrameData = EnvironmentGpuPlacementFrameData{};
        mVertexBufferViewCache.clear();
        mGpuDrivenFrameResources = {};
        mGpuInstanceContextCount = 0u;
        mGpuStatusUavIndex = InvalidDescriptorIndex;
        mLastGpuDispatchFuture = RenderContract::Future{};
        mGpuDrivenEnabled = false;
        mGpuResourcesInitialized = false;
    }
}
