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
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "Core/DX/DesciptorHeap.h"
#include "Environment/EnvironmentFoliageRuntime.h"
#include "RenderContract/Shadow/ShadowRenderContext.h"
#include "Utility/DirectXInclude.h"
#include "Utility/ErrorHandler.h"
#include "Widget/PerformanceProvider.h"

namespace Game {
    namespace {
        constexpr std::uint32_t EnvironmentGpuRootConstantDwordCount{ 64u };
        constexpr std::uint32_t EnvironmentGpuStatusDwordCount{ 16u };
        constexpr std::uint32_t EnvironmentComputeThreadGroupSize{ 64u };
        constexpr std::uint32_t EnvironmentDrawRecordGpuDrivenFlag{ 0x1u };
        constexpr std::uint32_t InvalidDescriptorIndex{ 0xffffffffu };
        constexpr float EnvironmentGpuCullRadius{ 18.0f };
        constexpr float EnvironmentGpuMaxDrawDistance{ 1000.0f };
        constexpr std::uint32_t EnvironmentGpuPrepareFailureInitialized{ 1u << 0u };
        constexpr std::uint32_t EnvironmentGpuPrepareFailureEnabled{ 1u << 1u };
        constexpr std::uint32_t EnvironmentGpuPrepareFailureResources{ 1u << 2u };
        constexpr std::uint32_t EnvironmentGpuPrepareFailureCopyQueue{ 1u << 3u };
        constexpr std::uint32_t EnvironmentGpuPrepareFailureComputeQueue{ 1u << 4u };
        constexpr std::uint32_t EnvironmentGpuPrepareFailureDrawRecords{ 1u << 5u };
        constexpr std::uint32_t EnvironmentGpuPrepareFailurePlacementDrawRecords{ 1u << 6u };
        constexpr std::uint32_t EnvironmentGpuPrepareFailureRules{ 1u << 7u };
        constexpr std::uint32_t EnvironmentGpuPrepareFailureCandidateRecords{ 1u << 8u };
        constexpr std::uint32_t EnvironmentGpuPrepareFailureCandidateDispatchRecords{ 1u << 9u };
        constexpr std::uint32_t EnvironmentGpuPrepareFailureCandidateCount{ 1u << 10u };
        constexpr std::uint32_t EnvironmentGpuPrepareFailureTerrainHeight{ 1u << 11u };
        constexpr std::uint32_t EnvironmentGpuPrepareFailureTerrainSplat0{ 1u << 12u };
        constexpr std::uint32_t EnvironmentGpuPrepareFailureTerrainSplat1{ 1u << 13u };
        constexpr std::uint32_t EnvironmentGpuPrepareFailureFoliageRuntime{ 1u << 14u };

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
            std::uint32_t mPlacementCandidateDispatchRecordSrvIndex{};
            std::uint32_t mCandidateDispatchRecordCount{};
            std::uint32_t mPlacementPointAtlasRecordSrvIndex{};
            std::uint32_t mPlacementPointAtlasPointSrvIndex{};
            std::uint32_t mPlacementDrawDispatchRecordSrvIndex{};
            std::uint32_t mDrawDispatchRecordCount{};
            std::uint32_t mCellMetadataSrvIndex{};
            std::uint32_t mCellMetadataUavIndex{};
            std::uint32_t mAcceptedCandidateSrvIndex{};
            std::uint32_t mAcceptedCandidateUavIndex{};
            std::uint32_t mCandidateDispatchRecordOffset{};
            std::uint32_t mFrustumPlanePadding1{};
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
            return std::tie(Left.mPass, Left.mPipeline, Left.mMesh, Left.mSubMesh, Left.mSegmentContextIndex, Left.mMaterialIndex, Left.mShadowCascadeMask, Left.mCastsShadow) < std::tie(Right.mPass, Right.mPipeline, Right.mMesh, Right.mSubMesh, Right.mSegmentContextIndex, Right.mMaterialIndex, Right.mShadowCascadeMask, Right.mCastsShadow);
        }

        bool IsEnvironmentBillboardRecord(const RenderContract::EnvironmentDrawRecord& DrawRecord) {
            return DrawRecord.mPipeline != nullptr && DrawRecord.mPipeline->GetPrimitiveTopology() == D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        }

        RenderContract::EnvironmentGpuDrivenDrawBatch BuildEnvironmentGpuDrivenDrawBatch(const RenderContract::EnvironmentDrawRecord& DrawRecord, std::uint32_t DrawRecordOffset, std::uint32_t DrawRecordCount) {
            RenderContract::EnvironmentGpuDrivenDrawBatch Batch{};
            Batch.mPipeline = DrawRecord.mPipeline;
            Batch.mMesh = DrawRecord.mMesh;
            Batch.mDrawRecordOffset = DrawRecordOffset;
            Batch.mDrawRecordCount = DrawRecordCount;
            Batch.mShadowCascadeMask = DrawRecord.mShadowCascadeMask;
            Batch.mCastsShadow = DrawRecord.mCastsShadow;
            Batch.mBillboard = IsEnvironmentBillboardRecord(DrawRecord);
            return Batch;
        }

        bool CanBatchEnvironmentGpuDrawRecords(const RenderContract::EnvironmentDrawRecord& Left, const RenderContract::EnvironmentDrawRecord& Right) {
            const bool IsLeftBillboard{ IsEnvironmentBillboardRecord(Left) };
            const bool IsRightBillboard{ IsEnvironmentBillboardRecord(Right) };
            return Left.mMesh != nullptr && Left.mInstanceCount > 0u && Right.mMesh != nullptr && Right.mInstanceCount > 0u && Left.mMesh == Right.mMesh && Left.mSubMesh == Right.mSubMesh && IsLeftBillboard == IsRightBillboard && (IsLeftBillboard == false || Left.mPipeline == Right.mPipeline);
        }

        void AppendEnvironmentGpuPrepareFailureText(std::string& Message, std::uint32_t FailureMask, std::uint32_t FailureFlag, const char* Text) {
            if ((FailureMask & FailureFlag) == 0u) {
                return;
            }

            if (Message.empty() == false) {
                Message += ", ";
            }

            Message += Text;
        }

        std::string BuildEnvironmentGpuPrepareFailureText(std::uint32_t FailureMask) {
            std::string Message{};
            AppendEnvironmentGpuPrepareFailureText(Message, FailureMask, EnvironmentGpuPrepareFailureInitialized, "RuntimeNotInitialized");
            AppendEnvironmentGpuPrepareFailureText(Message, FailureMask, EnvironmentGpuPrepareFailureEnabled, "GpuDrivenDisabled");
            AppendEnvironmentGpuPrepareFailureText(Message, FailureMask, EnvironmentGpuPrepareFailureResources, "GpuResourcesNotInitialized");
            AppendEnvironmentGpuPrepareFailureText(Message, FailureMask, EnvironmentGpuPrepareFailureCopyQueue, "MissingCopyQueue");
            AppendEnvironmentGpuPrepareFailureText(Message, FailureMask, EnvironmentGpuPrepareFailureComputeQueue, "MissingComputeQueue");
            AppendEnvironmentGpuPrepareFailureText(Message, FailureMask, EnvironmentGpuPrepareFailureDrawRecords, "EmptyDrawRecords");
            AppendEnvironmentGpuPrepareFailureText(Message, FailureMask, EnvironmentGpuPrepareFailurePlacementDrawRecords, "EmptyPlacementDrawRecords");
            AppendEnvironmentGpuPrepareFailureText(Message, FailureMask, EnvironmentGpuPrepareFailureRules, "EmptyRules");
            AppendEnvironmentGpuPrepareFailureText(Message, FailureMask, EnvironmentGpuPrepareFailureCandidateRecords, "EmptyCandidateRecords");
            AppendEnvironmentGpuPrepareFailureText(Message, FailureMask, EnvironmentGpuPrepareFailureCandidateDispatchRecords, "EmptyCandidateDispatchRecords");
            AppendEnvironmentGpuPrepareFailureText(Message, FailureMask, EnvironmentGpuPrepareFailureCandidateCount, "ZeroCandidateCount");
            AppendEnvironmentGpuPrepareFailureText(Message, FailureMask, EnvironmentGpuPrepareFailureTerrainHeight, "InvalidTerrainHeightSrv");
            AppendEnvironmentGpuPrepareFailureText(Message, FailureMask, EnvironmentGpuPrepareFailureTerrainSplat0, "InvalidTerrainSplat0Srv");
            AppendEnvironmentGpuPrepareFailureText(Message, FailureMask, EnvironmentGpuPrepareFailureTerrainSplat1, "InvalidTerrainSplat1Srv");
            AppendEnvironmentGpuPrepareFailureText(Message, FailureMask, EnvironmentGpuPrepareFailureFoliageRuntime, "MissingFoliageRuntime");
            return Message;
        }

        void ClearEnvironmentGpuPlacementFrameData(EnvironmentGpuPlacementFrameData& FrameData) {
            FrameData.mConfig = EnvironmentGpuPlacementConfig{};
            FrameData.mRules.clear();
            FrameData.mCandidateRecords.clear();
            FrameData.mCandidateDispatchRecords.clear();
            FrameData.mDrawDispatchRecords.clear();
            FrameData.mSpacingRuleRecords.clear();
            FrameData.mPointAtlasRecords.clear();
            FrameData.mPointAtlasPoints.clear();
            FrameData.mDrawRecords.clear();
            FrameData.mCandidateCount = 0u;
        }

        void BuildEnvironmentGpuDrivenGBufferDrawBatches(std::span<const RenderContract::EnvironmentDrawRecord> DrawRecords, std::vector<RenderContract::EnvironmentGpuDrivenDrawBatch>& OutBatches) {
            OutBatches.clear();
            std::size_t DrawRecordIndex{};
            while (DrawRecordIndex < DrawRecords.size()) {
                const RenderContract::EnvironmentDrawRecord& DrawRecord{ DrawRecords[DrawRecordIndex] };
                if (DrawRecord.mMesh == nullptr || DrawRecord.mInstanceCount == 0u) {
                    DrawRecordIndex += 1ULL;
                    continue;
                }

                std::size_t BatchCount{ 1ULL };
                while (DrawRecordIndex + BatchCount < DrawRecords.size() && CanBatchEnvironmentGpuDrawRecords(DrawRecord, DrawRecords[DrawRecordIndex + BatchCount]) == true) {
                    BatchCount += 1ULL;
                }

                OutBatches.push_back(BuildEnvironmentGpuDrivenDrawBatch(DrawRecord, static_cast<std::uint32_t>(DrawRecordIndex), static_cast<std::uint32_t>(BatchCount)));
                DrawRecordIndex += BatchCount;
            }
        }

        void BuildEnvironmentGpuDrivenShadowDrawBatches(std::span<const RenderContract::EnvironmentDrawRecord> DrawRecords, const RenderContract::ShadowMappingParameter& ShadowMappingParameter, std::array<std::vector<RenderContract::EnvironmentGpuDrivenDrawBatch>, RenderContract::ShadowCascadeMaxCount>& OutBatches) {
            for (std::vector<RenderContract::EnvironmentGpuDrivenDrawBatch>& Batches : OutBatches) {
                Batches.clear();
            }

            const std::uint32_t ShadowCascadeCount{ RenderContract::ResolveShadowCascadeCount(ShadowMappingParameter) };
            for (std::uint32_t CascadeIndex{}; CascadeIndex < ShadowCascadeCount; CascadeIndex += 1u) {
                const std::uint32_t ShadowCascadeBit{ CascadeIndex < 32u ? 1u << CascadeIndex : 0u };
                std::vector<RenderContract::EnvironmentGpuDrivenDrawBatch>& Batches{ OutBatches[CascadeIndex] };
                std::size_t DrawRecordIndex{};
                while (DrawRecordIndex < DrawRecords.size()) {
                    const RenderContract::EnvironmentDrawRecord& DrawRecord{ DrawRecords[DrawRecordIndex] };
                    if (DrawRecord.mMesh == nullptr || DrawRecord.mInstanceCount == 0u || DrawRecord.mCastsShadow == false || (DrawRecord.mShadowCascadeMask & ShadowCascadeBit) == 0u) {
                        DrawRecordIndex += 1ULL;
                        continue;
                    }

                    std::size_t BatchCount{ 1ULL };
                    while (DrawRecordIndex + BatchCount < DrawRecords.size()) {
                        const RenderContract::EnvironmentDrawRecord& NextDrawRecord{ DrawRecords[DrawRecordIndex + BatchCount] };
                        if (NextDrawRecord.mCastsShadow == false || (NextDrawRecord.mShadowCascadeMask & ShadowCascadeBit) == 0u || CanBatchEnvironmentGpuDrawRecords(DrawRecord, NextDrawRecord) == false) {
                            break;
                        }

                        BatchCount += 1ULL;
                    }

                    Batches.push_back(BuildEnvironmentGpuDrivenDrawBatch(DrawRecord, static_cast<std::uint32_t>(DrawRecordIndex), static_cast<std::uint32_t>(BatchCount)));
                    DrawRecordIndex += BatchCount;
                }
            }
        }

        struct EnvironmentGpuDrawBuildItem final {
        public:
            RenderContract::EnvironmentDrawRecord mDrawRecord{};
            EnvironmentGpuPlacementDrawRecord mPlacementDrawRecord{};
        };

        struct EnvironmentIndirectDrawCommand final {
        public:
            std::uint32_t mDrawRecordBaseIndex{};
            D3D12_DRAW_INDEXED_ARGUMENTS mDrawArguments{};
        };

        static_assert(sizeof(EnvironmentIndirectDrawCommand) == 24u);

        enum class EnvironmentGpuUploadHashIndex : std::size_t {
            SegmentContexts = 0ULL,
            DrawRecords,
            PlacementConfig,
            PlacementRules,
            PlacementDrawRecords,
            PlacementDrawDispatchRecords,
            PlacementCandidateRecords,
            PlacementCandidateDispatchRecords,
            PlacementSpacingRuleRecords,
            PlacementPointAtlasRecords,
            PlacementPointAtlasPoints,
            Count
        };

        enum class EnvironmentGpuPersistentUploadHashIndex : std::size_t {
            SegmentContexts = 0ULL,
            PlacementRules,
            PlacementSpacingRuleRecords,
            Count
        };

        enum class EnvironmentGpuSrvCacheIndex : std::size_t {
            InstanceContext = 0ULL,
            SegmentContext,
            DrawRecord,
            PlacementConfig,
            PlacementRule,
            PlacementDrawRecord,
            PlacementDrawDispatchRecord,
            PlacementCandidateRecord,
            PlacementCandidateDispatchRecord,
            PlacementSpacingRuleRecord,
            PlacementPointAtlasRecord,
            PlacementPointAtlasPoint,
            CandidateContext,
            VisibleInstanceIndex,
            Count
        };

        enum class EnvironmentGpuPersistentSrvCacheIndex : std::size_t {
            SegmentContext = 0ULL,
            PlacementRule,
            PlacementSpacingRuleRecord,
            CellMetadata,
            AcceptedCandidate,
            Count
        };

        enum class EnvironmentGpuUavCacheIndex : std::size_t {
            InstanceContext = 0ULL,
            CandidateContext,
            VisibleInstanceIndex,
            IndirectCommand,
            Count
        };

        enum class EnvironmentGpuPersistentUavCacheIndex : std::size_t {
            CellMetadata = 0ULL,
            AcceptedCandidate,
            Count
        };

        static_assert(static_cast<std::size_t>(EnvironmentGpuUploadHashIndex::Count) <= std::tuple_size_v<decltype(std::declval<EnvironmentGpuDrivenFrameResource>().mUploadedDataHashes)>);
        static_assert(static_cast<std::size_t>(EnvironmentGpuSrvCacheIndex::Count) <= std::tuple_size_v<decltype(std::declval<EnvironmentGpuDrivenFrameResource>().mSrvCaches)>);
        static_assert(static_cast<std::size_t>(EnvironmentGpuUavCacheIndex::Count) <= std::tuple_size_v<decltype(std::declval<EnvironmentGpuDrivenFrameResource>().mUavCaches)>);
        static_assert(static_cast<std::size_t>(EnvironmentGpuPersistentUploadHashIndex::Count) <= std::tuple_size_v<decltype(std::declval<EnvironmentGpuPersistentResource>().mUploadedDataHashes)>);
        static_assert(static_cast<std::size_t>(EnvironmentGpuPersistentSrvCacheIndex::Count) <= std::tuple_size_v<decltype(std::declval<EnvironmentGpuPersistentResource>().mSrvCaches)>);
        static_assert(static_cast<std::size_t>(EnvironmentGpuPersistentUavCacheIndex::Count) <= std::tuple_size_v<decltype(std::declval<EnvironmentGpuPersistentResource>().mUavCaches)>);

        std::uint64_t CalculateEnvironmentGpuDataHash(std::span<const std::byte> Data) {
            std::uint64_t Hash{ 14695981039346656037ULL };
            for (const std::byte Value : Data) {
                Hash ^= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(Value));
                Hash *= 1099511628211ULL;
            }

            return Hash == 0ULL ? 1ULL : Hash;
        }

        bool IsEnvironmentGpuDescriptorCacheValid(const EnvironmentGpuDescriptorCache& Cache, ID3D12Resource* Resource, std::uint32_t ElementCount, std::uint32_t Stride) {
            return Cache.mResource == Resource && Cache.mElementCount == ElementCount && Cache.mStride == Stride;
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

        std::uint32_t CalculateEnvironmentDispatchGroupCount(std::uint32_t ItemCount) {
            return (ItemCount + EnvironmentComputeThreadGroupSize - 1u) / EnvironmentComputeThreadGroupSize;
        }

        EnvironmentGpuPlacementDrawDispatchRecord BuildGpuPlacementDrawDispatchRecord(std::uint32_t DrawRecordIndex, std::int32_t CellX, std::int32_t CellZ, std::uint32_t InstanceOffset) {
            EnvironmentGpuPlacementDrawDispatchRecord DispatchRecord{};
            DispatchRecord.mDrawRecordIndex = DrawRecordIndex;
            DispatchRecord.mCellX = CellX;
            DispatchRecord.mCellZ = CellZ;
            DispatchRecord.mInstanceOffset = InstanceOffset;
            return DispatchRecord;
        }

        void BuildGpuPlacementDrawDispatchRecords(std::span<const EnvironmentGpuPlacementDrawRecord> DrawRecords, std::vector<EnvironmentGpuPlacementDrawDispatchRecord>& OutDispatchRecords) {
            OutDispatchRecords.clear();
            const std::uint32_t DrawRecordCount{ static_cast<std::uint32_t>(std::min<std::size_t>(DrawRecords.size(), std::numeric_limits<std::uint32_t>::max())) };
            for (std::uint32_t DrawRecordIndex{}; DrawRecordIndex < DrawRecordCount; DrawRecordIndex += 1u) {
                const EnvironmentGpuPlacementDrawRecord& DrawRecord{ DrawRecords[DrawRecordIndex] };
                if (DrawRecord.mCellCountX == 0u || DrawRecord.mCellCountZ == 0u || DrawRecord.mCandidateCount == 0u) {
                    continue;
                }

                const std::uint64_t CellCount{ static_cast<std::uint64_t>(DrawRecord.mCellCountX) * static_cast<std::uint64_t>(DrawRecord.mCellCountZ) };
                if (CellCount == 0ULL) {
                    continue;
                }

                const std::uint32_t CandidateCountPerCell{ static_cast<std::uint32_t>(std::max<std::uint64_t>(static_cast<std::uint64_t>(DrawRecord.mCandidateCount) / CellCount, 1ULL)) };
                for (std::uint32_t CellOffsetZ{}; CellOffsetZ < DrawRecord.mCellCountZ; CellOffsetZ += 1u) {
                    for (std::uint32_t CellOffsetX{}; CellOffsetX < DrawRecord.mCellCountX; CellOffsetX += 1u) {
                        for (std::uint32_t InstanceOffset{}; InstanceOffset < CandidateCountPerCell; InstanceOffset += EnvironmentComputeThreadGroupSize) {
                            OutDispatchRecords.push_back(BuildGpuPlacementDrawDispatchRecord(DrawRecordIndex, DrawRecord.mMinimumCellX + static_cast<std::int32_t>(CellOffsetX), DrawRecord.mMinimumCellZ + static_cast<std::int32_t>(CellOffsetZ), InstanceOffset));
                        }
                    }
                }
            }
        }

        std::uint32_t CalculateGpuPlacementCellMetadataCount(std::span<const EnvironmentGpuPlacementCandidateRecord> CandidateRecords) {
            std::uint64_t CellMetadataCount{};
            for (const EnvironmentGpuPlacementCandidateRecord& CandidateRecord : CandidateRecords) {
                if (CandidateRecord.mCellCountX == 0u || CandidateRecord.mCellCountZ == 0u) {
                    continue;
                }

                const std::uint64_t CellCount{ static_cast<std::uint64_t>(CandidateRecord.mCellCountX) * static_cast<std::uint64_t>(CandidateRecord.mCellCountZ) };
                const std::uint64_t CellMetadataEnd{ static_cast<std::uint64_t>(CandidateRecord.mCellMetadataOffset) + CellCount };
                CellMetadataCount = std::max(CellMetadataCount, CellMetadataEnd);
            }

            return static_cast<std::uint32_t>(std::min<std::uint64_t>(CellMetadataCount, std::numeric_limits<std::uint32_t>::max()));
        }

        EnvironmentGpuRootConstants BuildEnvironmentGpuRootConstants(const EnvironmentFrameInput& Input, std::uint32_t StatusUavIndex, std::uint32_t InstanceContextSrvIndex, std::uint32_t InstanceContextUavIndex, std::uint32_t DrawRecordSrvIndex, std::uint32_t PlacementConfigSrvIndex, std::uint32_t PlacementRuleSrvIndex, std::uint32_t PlacementDrawRecordSrvIndex, std::uint32_t PlacementCandidateRecordSrvIndex, std::uint32_t PlacementCandidateDispatchRecordSrvIndex, std::uint32_t PlacementPointAtlasRecordSrvIndex, std::uint32_t PlacementPointAtlasPointSrvIndex, std::uint32_t PlacementDrawDispatchRecordSrvIndex, std::uint32_t CandidateContextSrvIndex, std::uint32_t CandidateContextUavIndex, std::uint32_t IndirectArgumentUavIndex, std::uint32_t VisibleInstanceIndexUavIndex, std::uint32_t CellMetadataSrvIndex, std::uint32_t CellMetadataUavIndex, std::uint32_t AcceptedCandidateSrvIndex, std::uint32_t AcceptedCandidateUavIndex, std::uint32_t DrawRecordCount, std::uint32_t VisibleInstanceIndexCapacity, std::uint32_t CandidateRecordCount, std::uint32_t CandidateDispatchRecordCount, std::uint32_t DrawDispatchRecordCount, std::uint32_t SpacingRuleRecordCount) {
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
            Constants.mPlacementCandidateDispatchRecordSrvIndex = PlacementCandidateDispatchRecordSrvIndex;
            Constants.mCandidateDispatchRecordCount = CandidateDispatchRecordCount;
            Constants.mCandidateDispatchRecordOffset = 0u;
            Constants.mPlacementPointAtlasRecordSrvIndex = PlacementPointAtlasRecordSrvIndex;
            Constants.mPlacementPointAtlasPointSrvIndex = PlacementPointAtlasPointSrvIndex;
            Constants.mPlacementDrawDispatchRecordSrvIndex = PlacementDrawDispatchRecordSrvIndex;
            Constants.mDrawDispatchRecordCount = DrawDispatchRecordCount;
            Constants.mCellMetadataSrvIndex = CellMetadataSrvIndex;
            Constants.mCellMetadataUavIndex = CellMetadataUavIndex;
            Constants.mAcceptedCandidateSrvIndex = AcceptedCandidateSrvIndex;
            Constants.mAcceptedCandidateUavIndex = AcceptedCandidateUavIndex;

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
        mIndirectCommandInitializePipelineState{},
        mDenseCandidateGeneratePipelineState{},
        mSpacedCandidateGeneratePipelineState{},
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
        mGpuPersistentResource{},
        mGpuDrivenFrameResources{},
        mGpuInstanceContextCount{},
        mGpuStatusUavIndex{ InvalidDescriptorIndex },
        mLastGpuPrepareFailureMask{},
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
        mIndirectCommandInitializePipelineState{ std::move(Other.mIndirectCommandInitializePipelineState) },
        mDenseCandidateGeneratePipelineState{ std::move(Other.mDenseCandidateGeneratePipelineState) },
        mSpacedCandidateGeneratePipelineState{ std::move(Other.mSpacedCandidateGeneratePipelineState) },
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
        mGpuPersistentResource{ std::move(Other.mGpuPersistentResource) },
        mGpuDrivenFrameResources{ std::move(Other.mGpuDrivenFrameResources) },
        mGpuInstanceContextCount{ Other.mGpuInstanceContextCount },
        mGpuStatusUavIndex{ Other.mGpuStatusUavIndex },
        mLastGpuPrepareFailureMask{ Other.mLastGpuPrepareFailureMask },
        mInitialized{ Other.mInitialized },
        mGpuDrivenEnabled{ Other.mGpuDrivenEnabled },
        mGpuResourcesInitialized{ Other.mGpuResourcesInitialized } {
        Other.mDevice = nullptr;
        Other.mAllocator = nullptr;
        Other.mSrvHeap = nullptr;
        Other.mCopyQueue = nullptr;
        Other.mComputeQueue = nullptr;
        Other.mPhysicsAdapter = nullptr;
        Other.mIndirectCommandInitializePipelineState.Reset();
        Other.mDenseCandidateGeneratePipelineState.Reset();
        Other.mSpacedCandidateGeneratePipelineState.Reset();
        Other.mDrawIndexedIndirectCommandSignature.Reset();
        Other.mVertexBufferViewCache.clear();
        Other.mGpuDrivenFrameResources = {};
        Other.mGpuPlacementFrameData = EnvironmentGpuPlacementFrameData{};
        Other.mGpuPersistentResource = EnvironmentGpuPersistentResource{};
        Other.mGpuInstanceContextCount = 0u;
        Other.mGpuStatusUavIndex = InvalidDescriptorIndex;
        Other.mLastGpuPrepareFailureMask = 0u;
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
        mIndirectCommandInitializePipelineState = std::move(Other.mIndirectCommandInitializePipelineState);
        mDenseCandidateGeneratePipelineState = std::move(Other.mDenseCandidateGeneratePipelineState);
        mSpacedCandidateGeneratePipelineState = std::move(Other.mSpacedCandidateGeneratePipelineState);
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
        mGpuPersistentResource = std::move(Other.mGpuPersistentResource);
        mGpuDrivenFrameResources = std::move(Other.mGpuDrivenFrameResources);
        mGpuInstanceContextCount = Other.mGpuInstanceContextCount;
        mGpuStatusUavIndex = Other.mGpuStatusUavIndex;
        mLastGpuPrepareFailureMask = Other.mLastGpuPrepareFailureMask;
        mInitialized = Other.mInitialized;
        mGpuDrivenEnabled = Other.mGpuDrivenEnabled;
        mGpuResourcesInitialized = Other.mGpuResourcesInitialized;
        Other.mDevice = nullptr;
        Other.mAllocator = nullptr;
        Other.mSrvHeap = nullptr;
        Other.mCopyQueue = nullptr;
        Other.mComputeQueue = nullptr;
        Other.mPhysicsAdapter = nullptr;
        Other.mIndirectCommandInitializePipelineState.Reset();
        Other.mDenseCandidateGeneratePipelineState.Reset();
        Other.mSpacedCandidateGeneratePipelineState.Reset();
        Other.mDrawIndexedIndirectCommandSignature.Reset();
        Other.mVertexBufferViewCache.clear();
        Other.mGpuDrivenFrameResources = {};
        Other.mGpuPlacementFrameData = EnvironmentGpuPlacementFrameData{};
        Other.mGpuPersistentResource = EnvironmentGpuPersistentResource{};
        Other.mGpuInstanceContextCount = 0u;
        Other.mGpuStatusUavIndex = InvalidDescriptorIndex;
        Other.mLastGpuPrepareFailureMask = 0u;
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
        ClearEnvironmentGpuPlacementFrameData(mGpuPlacementFrameData);
        if (mFoliageRuntime != nullptr) {
            Widget::PerformanceProvider::Get().BeginPhaseProfile("EnvironmentGpuBuildRenderData");
            mFoliageRuntime->BuildGpuDrivenRenderData(Input.mFocusPosition, RenderData, mGpuPlacementFrameData);
            Widget::PerformanceProvider::Get().EndPhaseProfile();
        }

        std::uint32_t FailureMask{};
        FailureMask |= mInitialized == false ? EnvironmentGpuPrepareFailureInitialized : 0u;
        FailureMask |= mGpuDrivenEnabled == false ? EnvironmentGpuPrepareFailureEnabled : 0u;
        FailureMask |= mGpuResourcesInitialized == false ? EnvironmentGpuPrepareFailureResources : 0u;
        FailureMask |= mCopyQueue == nullptr ? EnvironmentGpuPrepareFailureCopyQueue : 0u;
        FailureMask |= mComputeQueue == nullptr ? EnvironmentGpuPrepareFailureComputeQueue : 0u;
        FailureMask |= mFoliageRuntime == nullptr ? EnvironmentGpuPrepareFailureFoliageRuntime : 0u;
        FailureMask |= RenderData.mEnvironmentDrawRecords.empty() == true ? EnvironmentGpuPrepareFailureDrawRecords : 0u;
        FailureMask |= mGpuPlacementFrameData.mDrawRecords.empty() == true ? EnvironmentGpuPrepareFailurePlacementDrawRecords : 0u;
        FailureMask |= mGpuPlacementFrameData.mRules.empty() == true ? EnvironmentGpuPrepareFailureRules : 0u;
        FailureMask |= mGpuPlacementFrameData.mCandidateRecords.empty() == true ? EnvironmentGpuPrepareFailureCandidateRecords : 0u;
        FailureMask |= mGpuPlacementFrameData.mCandidateCount == 0u ? EnvironmentGpuPrepareFailureCandidateCount : 0u;
        FailureMask |= Input.mTerrain.mHeightSrvIndex == InvalidDescriptorIndex ? EnvironmentGpuPrepareFailureTerrainHeight : 0u;
        FailureMask |= Input.mTerrain.mSplatSrvIndex == InvalidDescriptorIndex ? EnvironmentGpuPrepareFailureTerrainSplat0 : 0u;
        FailureMask |= Input.mTerrain.mSplat1SrvIndex == InvalidDescriptorIndex ? EnvironmentGpuPrepareFailureTerrainSplat1 : 0u;
        if (FailureMask != 0u) {
            if (mLastGpuPrepareFailureMask != FailureMask) {
                const std::string FailureText{ BuildEnvironmentGpuPrepareFailureText(FailureMask) };
                ErrorHandler::report("EnvironmentRuntime", FailureText, ErrorHandler::Level::Warning);
                mLastGpuPrepareFailureMask = FailureMask;
            }

            mLastGpuDispatchFuture = RenderContract::Future{};
            return mLastGpuDispatchFuture;
        }

        mLastGpuPrepareFailureMask = 0u;
        std::uint32_t VisibleInstanceIndexCount{};
        Widget::PerformanceProvider::Get().BeginPhaseProfile("EnvironmentGpuBuildFrameData");
        BuildGpuDrivenFrameData(Input, RenderData, VisibleInstanceIndexCount);
        Widget::PerformanceProvider::Get().EndPhaseProfile();
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
        const std::uint32_t PlacementDrawDispatchRecordCount{ static_cast<std::uint32_t>(mGpuPlacementFrameData.mDrawDispatchRecords.size()) };
        const std::uint32_t PlacementCandidateRecordCount{ static_cast<std::uint32_t>(mGpuPlacementFrameData.mCandidateRecords.size()) };
        const std::uint32_t PlacementCandidateDispatchRecordCount{ static_cast<std::uint32_t>(mGpuPlacementFrameData.mCandidateDispatchRecords.size()) };
        const std::uint32_t PlacementSpacingRuleRecordCount{ static_cast<std::uint32_t>(mGpuPlacementFrameData.mSpacingRuleRecords.size()) };
        const std::uint32_t PlacementPointAtlasRecordCount{ static_cast<std::uint32_t>(mGpuPlacementFrameData.mPointAtlasRecords.size()) };
        const std::uint32_t PlacementPointAtlasPointCount{ static_cast<std::uint32_t>(mGpuPlacementFrameData.mPointAtlasPoints.size()) };
        const std::uint32_t CandidateContextCount{ mGpuPlacementFrameData.mCandidateCount };
        const std::uint32_t CellMetadataCount{ CalculateGpuPlacementCellMetadataCount(std::span<const EnvironmentGpuPlacementCandidateRecord>{ mGpuPlacementFrameData.mCandidateRecords.data(), mGpuPlacementFrameData.mCandidateRecords.size() }) };
        const std::uint32_t AcceptedCandidateCount{ CandidateContextCount };
        const std::size_t FrameResourceIndex{ static_cast<std::size_t>(Input.mFrameIndex % Constants::FrameCount<std::uint64_t>) };
        EnvironmentGpuDrivenFrameResource& FrameResource{ mGpuDrivenFrameResources[FrameResourceIndex] };
        Widget::PerformanceProvider::Get().BeginPhaseProfile("EnvironmentGpuEnsureResources");
        if (EnsureGpuPersistentResources(mGpuPersistentResource, SegmentContextCount, PlacementRuleCount, PlacementSpacingRuleRecordCount, CellMetadataCount, AcceptedCandidateCount) == false || EnsureGpuDrivenFrameResources(FrameResource, InstanceContextCount, 1u, DrawRecordCount, PlacementConfigCount, 1u, PlacementDrawRecordCount, PlacementDrawDispatchRecordCount, PlacementCandidateRecordCount, PlacementCandidateDispatchRecordCount, 1u, PlacementPointAtlasRecordCount, PlacementPointAtlasPointCount, CandidateContextCount, VisibleInstanceIndexCount) == false) {
            Widget::PerformanceProvider::Get().EndPhaseProfile();
            mLastGpuDispatchFuture = RenderContract::Future{};
            return mLastGpuDispatchFuture;
        }
        Widget::PerformanceProvider::Get().EndPhaseProfile();

        Widget::PerformanceProvider::Get().BeginPhaseProfile("EnvironmentGpuUpdateDescriptors");
        UpdateGpuPersistentShaderResourceViews(mGpuPersistentResource, SegmentContextCount, PlacementRuleCount, PlacementSpacingRuleRecordCount, CellMetadataCount, AcceptedCandidateCount);
        UpdateGpuPersistentUnorderedAccessViews(mGpuPersistentResource, CellMetadataCount, AcceptedCandidateCount);
        UpdateGpuDrivenShaderResourceViews(FrameResource, InstanceContextCount, 1u, DrawRecordCount, PlacementConfigCount, 1u, PlacementDrawRecordCount, PlacementDrawDispatchRecordCount, PlacementCandidateRecordCount, PlacementCandidateDispatchRecordCount, 1u, PlacementPointAtlasRecordCount, PlacementPointAtlasPointCount, CandidateContextCount, VisibleInstanceIndexCount);
        UpdateGpuDrivenUnorderedAccessViews(FrameResource, InstanceContextCount, CandidateContextCount, VisibleInstanceIndexCount, DrawRecordCount);
        Widget::PerformanceProvider::Get().EndPhaseProfile();

        Widget::PerformanceProvider::Get().BeginPhaseProfile("EnvironmentGpuUploadFrameData");
        const RenderContract::Future PersistentCopyFuture{ UploadGpuPersistentData(mGpuPersistentResource) };
        const RenderContract::Future FrameCopyFuture{ UploadGpuDrivenFrameData(FrameResource) };
        const std::array<RenderContract::Future, 2> CopyFutures{ PersistentCopyFuture, FrameCopyFuture };
        const RenderContract::Future CopyFuture{ RenderContract::Future::Merge(CopyFutures) };
        Widget::PerformanceProvider::Get().EndPhaseProfile();
        Widget::PerformanceProvider::Get().BeginPhaseProfile("EnvironmentGpuEnqueueCompute");
        mLastGpuDispatchFuture = DispatchGpuDrivenFrame(FrameResource, Input, CopyFuture, DrawRecordCount, VisibleInstanceIndexCount, PlacementCandidateRecordCount, PlacementCandidateDispatchRecordCount, mGpuPlacementFrameData.mDenseCandidateDispatchRecordCount, mGpuPlacementFrameData.mSpacedCandidateDispatchRecordCount, PlacementDrawDispatchRecordCount, PlacementSpacingRuleRecordCount);
        Widget::PerformanceProvider::Get().EndPhaseProfile();
        FillGpuDrivenFramePayload(FrameResource, RenderData, mLastGpuDispatchFuture);
        return mLastGpuDispatchFuture;
    }

    RenderContract::Future EnvironmentRuntime::DispatchGpu(const EnvironmentFrameInput& Input) {
        if (mInitialized == false || mGpuDrivenEnabled == false || mGpuResourcesInitialized == false || mComputeQueue == nullptr || mSrvHeap == nullptr || mGpuStatusUavIndex == InvalidDescriptorIndex) {
            mLastGpuDispatchFuture = RenderContract::Future{};
            return mLastGpuDispatchFuture;
        }

        const EnvironmentGpuRootConstants RootConstants{ BuildEnvironmentGpuRootConstants(Input, mGpuStatusUavIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, InvalidDescriptorIndex, 0u, 0u, 0u, 0u, 0u, 0u) };

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
        if (Context.mCommandList == nullptr || Context.mRenderFrameData == nullptr) {
            return;
        }

        if (Context.mRenderFrameData->mEnvironmentGpuDrivenFrame.mEnabled == true) {
            RecordGBufferIndirect(Context);
            return;
        }

        if (Context.mRenderFrameData->mEnvironmentDrawRecords.empty() == true) {
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

        const std::uint32_t ShadowCascadeBit{ Context.mShadowFrameGlobalsIndex < 32u ? 1u << Context.mShadowFrameGlobalsIndex : 0u };
        const RenderContract::IPipeline* ActivePipeline{ nullptr };
        for (std::size_t DrawRecordIndex{}; DrawRecordIndex < Context.mShadowRenderContext->mEnvironmentDrawRecords.size(); DrawRecordIndex += 1ULL) {
            const RenderContract::EnvironmentDrawRecord& DrawRecord{ Context.mShadowRenderContext->mEnvironmentDrawRecords[DrawRecordIndex] };
            if (DrawRecord.mMesh == nullptr || DrawRecord.mInstanceCount == 0u || DrawRecord.mCastsShadow == false || (DrawRecord.mShadowCascadeMask & ShadowCascadeBit) == 0u) {
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

    bool EnvironmentRuntime::EnsureDrawIndexedIndirectCommandSignature(ID3D12RootSignature* RootSignature) {
        if (mDrawIndexedIndirectCommandSignature != nullptr) {
            return true;
        }

        if (mDevice == nullptr || RootSignature == nullptr) {
            return false;
        }

        std::array<D3D12_INDIRECT_ARGUMENT_DESC, 2> ArgumentDescs{};
        ArgumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
        ArgumentDescs[0].Constant.RootParameterIndex = 0u;
        ArgumentDescs[0].Constant.DestOffsetIn32BitValues = 4u;
        ArgumentDescs[0].Constant.Num32BitValuesToSet = 1u;
        ArgumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

        D3D12_COMMAND_SIGNATURE_DESC CommandSignatureDesc{};
        CommandSignatureDesc.ByteStride = sizeof(EnvironmentIndirectDrawCommand);
        CommandSignatureDesc.NumArgumentDescs = static_cast<UINT>(ArgumentDescs.size());
        CommandSignatureDesc.pArgumentDescs = ArgumentDescs.data();
        CommandSignatureDesc.NodeMask = 0u;

        const HRESULT CreateResult{ mDevice->CreateCommandSignature(&CommandSignatureDesc, RootSignature, IID_PPV_ARGS(mDrawIndexedIndirectCommandSignature.GetAddressOf())) };
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
        if (GpuFrame.mEnabled == false || GpuFrame.mGBufferDrawBatches.empty() == true || GpuFrame.mVisibleInstanceIndexResource == nullptr || GpuFrame.mIndirectArgumentResource == nullptr) {
            return;
        }

        Widget::PerformanceProvider::Get().BeginPhaseProfile("EnvironmentGpuRecordGBufferIndirect");
        const std::uint32_t ShadowCascadeCount{ RenderContract::ResolveShadowCascadeCount(Context.mRenderFrameData->mShadowMappingParameter) };
        if (ShadowCascadeCount == 0u) {
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
        }

        const RenderContract::IPipeline* ObjectPipeline{};
        bool IsObjectPipelineResolved{};
        const RenderContract::IPipeline* ActivePipeline{ nullptr };
        for (const RenderContract::EnvironmentGpuDrivenDrawBatch& Batch : GpuFrame.mGBufferDrawBatches) {
            if (Batch.mMesh == nullptr || Batch.mDrawRecordCount == 0u || Batch.mDrawRecordOffset >= GpuFrame.mDrawRecordCount) {
                continue;
            }

            if (Batch.mBillboard == false && IsObjectPipelineResolved == false) {
                ObjectPipeline = Context.mPipelineProvider->ResolveEnvironmentObjectPipeline();
                IsObjectPipelineResolved = true;
            }

            const RenderContract::IPipeline* Pipeline{ Batch.mBillboard == true ? Batch.mPipeline : ObjectPipeline };
            if (Pipeline == nullptr || EnsureDrawIndexedIndirectCommandSignature(Pipeline->GetRootSignature()) == false) {
                continue;
            }

            ActivePipeline = Pipeline->Set(ActivePipeline, Context.mCommandList);

            DrawRootConstantsB1 RootConstants{};
            RootConstants.mFrameGlobalsSrvIndex = Context.mFrameGlobalsSrvIndex;
            RootConstants.mModelContextSrvIndex = GpuFrame.mInstanceContextSrvIndex;
            RootConstants.mBonePaletteSrvIndex = GpuFrame.mSegmentContextSrvIndex;
            RootConstants.mDrawRecordSrvIndex = GpuFrame.mDrawRecordSrvIndex;
            RootConstants.mDrawRecordBaseIndex = Batch.mDrawRecordOffset;
            RootConstants.mMaterialSrvIndex = Context.mMaterialSrvIndex;
            RootConstants.mMaterialTextureTableSrvIndex = Context.mMaterialTextureTableSrvIndex;
            RootConstants.mShadowMappingParameterSrvIndex = InvalidDescriptorIndex;
            RootConstants.mShadowMapTextureBaseSrvIndex = InvalidDescriptorIndex;
            RootConstants.mFrameGlobalsElementIndex = 0u;
            RootConstants.mTerrainPatchContextSrvIndex = InvalidDescriptorIndex;
            RootConstants.mReserved1 = GpuFrame.mVisibleInstanceIndexSrvIndex;
            Context.mCommandList->SetGraphicsRoot32BitConstants(0, sizeof(DrawRootConstantsB1) / sizeof(std::uint32_t), &RootConstants, 0);

            Context.mCommandList->IASetPrimitiveTopology(Pipeline->GetPrimitiveTopology());

            const std::vector<D3D12_VERTEX_BUFFER_VIEW>& VertexBufferViews{ ResolveVertexBufferViews(*Pipeline, *Batch.mMesh) };
            if (VertexBufferViews.empty() == false) {
                Context.mCommandList->IASetVertexBuffers(0, static_cast<UINT>(VertexBufferViews.size()), VertexBufferViews.data());
            }

            const D3D12_INDEX_BUFFER_VIEW& IndexBufferView{ Batch.mMesh->GetIndexBufferView() };
            Context.mCommandList->IASetIndexBuffer(&IndexBufferView);

            const std::uint32_t SafeDrawRecordCount{ std::min<std::uint32_t>(Batch.mDrawRecordCount, GpuFrame.mDrawRecordCount - Batch.mDrawRecordOffset) };
            const std::uint64_t IndirectArgumentOffset{ sizeof(EnvironmentIndirectDrawCommand) * static_cast<std::uint64_t>(Batch.mDrawRecordOffset) };
            Context.mCommandList->ExecuteIndirect(mDrawIndexedIndirectCommandSignature.Get(), SafeDrawRecordCount, GpuFrame.mIndirectArgumentResource, IndirectArgumentOffset, nullptr, 0u);
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
        Widget::PerformanceProvider::Get().EndPhaseProfile();
    }

    void EnvironmentRuntime::RecordShadowDepthIndirect(const RenderContract::EnvironmentShadowDepthRenderCommandContext& Context) {
        if (Context.mRenderFrameData == nullptr || Context.mPipelineProvider == nullptr) {
            return;
        }

        const RenderContract::EnvironmentGpuDrivenFrameData& GpuFrame{ Context.mRenderFrameData->mEnvironmentGpuDrivenFrame };
        if (GpuFrame.mEnabled == false || GpuFrame.mInstanceContextResource == nullptr || GpuFrame.mVisibleInstanceIndexResource == nullptr || GpuFrame.mIndirectArgumentResource == nullptr) {
            return;
        }

        Widget::PerformanceProvider::Get().BeginPhaseProfile("EnvironmentGpuRecordShadowIndirect");
        if (Context.mShadowFrameGlobalsIndex == 0u) {
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
        }

        if (Context.mShadowFrameGlobalsIndex >= GpuFrame.mShadowDrawBatches.size() || GpuFrame.mShadowDrawBatches[Context.mShadowFrameGlobalsIndex].empty() == true) {
            Widget::PerformanceProvider::Get().EndPhaseProfile();
            return;
        }

        const RenderContract::IPipeline* ObjectDepthPipeline{};
        const RenderContract::IPipeline* BillboardDepthPipeline{};
        bool IsObjectDepthPipelineResolved{};
        bool IsBillboardDepthPipelineResolved{};
        const RenderContract::IPipeline* ActivePipeline{ nullptr };
        if (Context.mDynamicDepthBiasCommandList != nullptr) {
            Context.mDynamicDepthBiasCommandList->RSSetDepthBias(Context.mRasterDepthBias, Context.mRasterDepthBiasClamp, Context.mRasterSlopeScaledDepthBias);
        }

        for (const RenderContract::EnvironmentGpuDrivenDrawBatch& Batch : GpuFrame.mShadowDrawBatches[Context.mShadowFrameGlobalsIndex]) {
            if (Batch.mMesh == nullptr || Batch.mDrawRecordCount == 0u || Batch.mDrawRecordOffset >= GpuFrame.mDrawRecordCount) {
                continue;
            }

            if (Batch.mBillboard == true && IsBillboardDepthPipelineResolved == false) {
                BillboardDepthPipeline = Context.mPipelineProvider->ResolveEnvironmentBillboardDepthPipeline();
                IsBillboardDepthPipelineResolved = true;
            }

            if (Batch.mBillboard == false && IsObjectDepthPipelineResolved == false) {
                ObjectDepthPipeline = Context.mPipelineProvider->ResolveEnvironmentObjectDepthPipeline();
                IsObjectDepthPipelineResolved = true;
            }

            const RenderContract::IPipeline* Pipeline{ Batch.mBillboard == true ? BillboardDepthPipeline : ObjectDepthPipeline };
            if (Pipeline == nullptr || EnsureDrawIndexedIndirectCommandSignature(Pipeline->GetRootSignature()) == false) {
                continue;
            }

            ActivePipeline = Pipeline->Set(ActivePipeline, Context.mCommandList);

            DrawRootConstantsB1 RootConstants{};
            RootConstants.mFrameGlobalsSrvIndex = Context.mFrameGlobalsSrvIndex;
            RootConstants.mModelContextSrvIndex = GpuFrame.mInstanceContextSrvIndex;
            RootConstants.mBonePaletteSrvIndex = GpuFrame.mSegmentContextSrvIndex;
            RootConstants.mDrawRecordSrvIndex = GpuFrame.mDrawRecordSrvIndex;
            RootConstants.mDrawRecordBaseIndex = Batch.mDrawRecordOffset;
            RootConstants.mMaterialSrvIndex = Context.mMaterialSrvIndex;
            RootConstants.mMaterialTextureTableSrvIndex = Context.mMaterialTextureTableSrvIndex;
            RootConstants.mShadowMappingParameterSrvIndex = InvalidDescriptorIndex;
            RootConstants.mShadowMapTextureBaseSrvIndex = InvalidDescriptorIndex;
            RootConstants.mFrameGlobalsElementIndex = Context.mShadowFrameGlobalsIndex;
            RootConstants.mTerrainPatchContextSrvIndex = InvalidDescriptorIndex;
            RootConstants.mReserved1 = GpuFrame.mVisibleInstanceIndexSrvIndex;
            Context.mCommandList->SetGraphicsRoot32BitConstants(0, sizeof(DrawRootConstantsB1) / sizeof(std::uint32_t), &RootConstants, 0);

            Context.mCommandList->IASetPrimitiveTopology(Pipeline->GetPrimitiveTopology());

            const std::vector<D3D12_VERTEX_BUFFER_VIEW>& VertexBufferViews{ ResolveVertexBufferViews(*Pipeline, *Batch.mMesh) };
            if (VertexBufferViews.empty() == false) {
                Context.mCommandList->IASetVertexBuffers(0, static_cast<UINT>(VertexBufferViews.size()), VertexBufferViews.data());
            }

            const D3D12_INDEX_BUFFER_VIEW& IndexBufferView{ Batch.mMesh->GetIndexBufferView() };
            Context.mCommandList->IASetIndexBuffer(&IndexBufferView);

            const std::uint32_t SafeDrawRecordCount{ std::min<std::uint32_t>(Batch.mDrawRecordCount, GpuFrame.mDrawRecordCount - Batch.mDrawRecordOffset) };
            const std::uint64_t IndirectArgumentOffset{ sizeof(EnvironmentIndirectDrawCommand) * static_cast<std::uint64_t>(Batch.mDrawRecordOffset) };
            Context.mCommandList->ExecuteIndirect(mDrawIndexedIndirectCommandSignature.Get(), SafeDrawRecordCount, GpuFrame.mIndirectArgumentResource, IndirectArgumentOffset, nullptr, 0u);
        }
        Widget::PerformanceProvider::Get().EndPhaseProfile();
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

        if (CreateEnvironmentComputePipelineState(mDevice, mComputeRootSignature.Get(), "cs_6_6:InitializeIndirectCommandsCsMain", mIndirectCommandInitializePipelineState) == false) {
            return false;
        }

        if (CreateEnvironmentComputePipelineState(mDevice, mComputeRootSignature.Get(), "cs_6_6:GenerateDenseCandidatesCsMain", mDenseCandidateGeneratePipelineState) == false) {
            return false;
        }

        if (CreateEnvironmentComputePipelineState(mDevice, mComputeRootSignature.Get(), "cs_6_6:GenerateSpacedCandidatesCsMain", mSpacedCandidateGeneratePipelineState) == false) {
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

        if (FrameResource.mPlacementDrawDispatchRecordSrvHandle.IsValid() == false) {
            FrameResource.mPlacementDrawDispatchRecordSrvHandle = mSrvHeap->Allocate();
        }

        if (FrameResource.mPlacementCandidateRecordSrvHandle.IsValid() == false) {
            FrameResource.mPlacementCandidateRecordSrvHandle = mSrvHeap->Allocate();
        }

        if (FrameResource.mPlacementCandidateDispatchRecordSrvHandle.IsValid() == false) {
            FrameResource.mPlacementCandidateDispatchRecordSrvHandle = mSrvHeap->Allocate();
        }

        if (FrameResource.mPlacementSpacingRuleRecordSrvHandle.IsValid() == false) {
            FrameResource.mPlacementSpacingRuleRecordSrvHandle = mSrvHeap->Allocate();
        }

        if (FrameResource.mPlacementPointAtlasRecordSrvHandle.IsValid() == false) {
            FrameResource.mPlacementPointAtlasRecordSrvHandle = mSrvHeap->Allocate();
        }

        if (FrameResource.mPlacementPointAtlasPointSrvHandle.IsValid() == false) {
            FrameResource.mPlacementPointAtlasPointSrvHandle = mSrvHeap->Allocate();
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

        return FrameResource.mInstanceContextSrvHandle.IsValid() == true && FrameResource.mInstanceContextUavHandle.IsValid() == true && FrameResource.mSegmentContextSrvHandle.IsValid() == true && FrameResource.mDrawRecordSrvHandle.IsValid() == true && FrameResource.mPlacementConfigSrvHandle.IsValid() == true && FrameResource.mPlacementRuleSrvHandle.IsValid() == true && FrameResource.mPlacementDrawRecordSrvHandle.IsValid() == true && FrameResource.mPlacementDrawDispatchRecordSrvHandle.IsValid() == true && FrameResource.mPlacementCandidateRecordSrvHandle.IsValid() == true && FrameResource.mPlacementCandidateDispatchRecordSrvHandle.IsValid() == true && FrameResource.mPlacementSpacingRuleRecordSrvHandle.IsValid() == true && FrameResource.mPlacementPointAtlasRecordSrvHandle.IsValid() == true && FrameResource.mPlacementPointAtlasPointSrvHandle.IsValid() == true && FrameResource.mCandidateContextSrvHandle.IsValid() == true && FrameResource.mCandidateContextUavHandle.IsValid() == true && FrameResource.mVisibleInstanceIndexSrvHandle.IsValid() == true && FrameResource.mVisibleInstanceIndexUavHandle.IsValid() == true && FrameResource.mIndirectArgumentUavHandle.IsValid() == true;
    }

    bool EnvironmentRuntime::EnsureGpuPersistentDescriptorHandles(EnvironmentGpuPersistentResource& PersistentResource) {
        if (mSrvHeap == nullptr) {
            return false;
        }

        if (PersistentResource.mSegmentContextSrvHandle.IsValid() == false) {
            PersistentResource.mSegmentContextSrvHandle = mSrvHeap->Allocate();
        }

        if (PersistentResource.mPlacementRuleSrvHandle.IsValid() == false) {
            PersistentResource.mPlacementRuleSrvHandle = mSrvHeap->Allocate();
        }

        if (PersistentResource.mPlacementSpacingRuleRecordSrvHandle.IsValid() == false) {
            PersistentResource.mPlacementSpacingRuleRecordSrvHandle = mSrvHeap->Allocate();
        }

        if (PersistentResource.mCellMetadataSrvHandle.IsValid() == false) {
            PersistentResource.mCellMetadataSrvHandle = mSrvHeap->Allocate();
        }

        if (PersistentResource.mCellMetadataUavHandle.IsValid() == false) {
            PersistentResource.mCellMetadataUavHandle = mSrvHeap->Allocate();
        }

        if (PersistentResource.mAcceptedCandidateSrvHandle.IsValid() == false) {
            PersistentResource.mAcceptedCandidateSrvHandle = mSrvHeap->Allocate();
        }

        if (PersistentResource.mAcceptedCandidateUavHandle.IsValid() == false) {
            PersistentResource.mAcceptedCandidateUavHandle = mSrvHeap->Allocate();
        }

        return PersistentResource.mSegmentContextSrvHandle.IsValid() == true && PersistentResource.mPlacementRuleSrvHandle.IsValid() == true && PersistentResource.mPlacementSpacingRuleRecordSrvHandle.IsValid() == true && PersistentResource.mCellMetadataSrvHandle.IsValid() == true && PersistentResource.mCellMetadataUavHandle.IsValid() == true && PersistentResource.mAcceptedCandidateSrvHandle.IsValid() == true && PersistentResource.mAcceptedCandidateUavHandle.IsValid() == true;
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

    bool EnvironmentRuntime::EnsureGpuPersistentResources(EnvironmentGpuPersistentResource& PersistentResource, std::uint32_t SegmentContextCount, std::uint32_t PlacementRuleCount, std::uint32_t PlacementSpacingRuleRecordCount, std::uint32_t CellMetadataCount, std::uint32_t AcceptedCandidateCount) {
        if (EnsureGpuPersistentDescriptorHandles(PersistentResource) == false) {
            return false;
        }

        const std::uint32_t SafeSegmentContextCount{ std::max(SegmentContextCount, 1u) };
        const std::uint32_t SafePlacementRuleCount{ std::max(PlacementRuleCount, 1u) };
        const std::uint32_t SafePlacementSpacingRuleRecordCount{ std::max(PlacementSpacingRuleRecordCount, 1u) };
        const std::uint32_t SafeCellMetadataCount{ std::max(CellMetadataCount, 1u) };
        const std::uint32_t SafeAcceptedCandidateCount{ std::max(AcceptedCandidateCount, 1u) };
        ID3D12Resource* PreviousSegmentContextBuffer{ PersistentResource.mSegmentContextBuffer.Get() };
        ID3D12Resource* PreviousPlacementRuleBuffer{ PersistentResource.mPlacementRuleBuffer.Get() };
        ID3D12Resource* PreviousPlacementSpacingRuleRecordBuffer{ PersistentResource.mPlacementSpacingRuleRecordBuffer.Get() };
        ID3D12Resource* PreviousCellMetadataBuffer{ PersistentResource.mCellMetadataBuffer.Get() };
        ID3D12Resource* PreviousAcceptedCandidateBuffer{ PersistentResource.mAcceptedCandidateBuffer.Get() };

        bool Result{ true };
        Result = EnsureGpuDrivenBuffer(PersistentResource.mSegmentContextBuffer, PersistentResource.mSegmentContextBufferCapacityInBytes, sizeof(RenderContract::EnvironmentSegmentContext) * static_cast<std::uint64_t>(SafeSegmentContextCount), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.PersistentSegmentContextBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(PersistentResource.mPlacementRuleBuffer, PersistentResource.mPlacementRuleBufferCapacityInBytes, sizeof(EnvironmentGpuPlacementRule) * static_cast<std::uint64_t>(SafePlacementRuleCount), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.PersistentPlacementRuleBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(PersistentResource.mPlacementSpacingRuleRecordBuffer, PersistentResource.mPlacementSpacingRuleRecordBufferCapacityInBytes, sizeof(EnvironmentGpuPlacementSpacingRuleRecord) * static_cast<std::uint64_t>(SafePlacementSpacingRuleRecordCount), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.PersistentPlacementSpacingRuleRecordBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(PersistentResource.mCellMetadataBuffer, PersistentResource.mCellMetadataBufferCapacityInBytes, sizeof(EnvironmentGpuPlacementCellMetadata) * static_cast<std::uint64_t>(SafeCellMetadataCount), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.PersistentCellMetadataBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(PersistentResource.mAcceptedCandidateBuffer, PersistentResource.mAcceptedCandidateBufferCapacityInBytes, sizeof(EnvironmentGpuPlacementCandidate) * static_cast<std::uint64_t>(SafeAcceptedCandidateCount), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.PersistentAcceptedCandidateBuffer") && Result;
        if (PersistentResource.mCellMetadataBuffer.Get() != PreviousCellMetadataBuffer) {
            PersistentResource.mCellMetadataState = D3D12_RESOURCE_STATE_COMMON;
        }

        if (PersistentResource.mAcceptedCandidateBuffer.Get() != PreviousAcceptedCandidateBuffer) {
            PersistentResource.mAcceptedCandidateState = D3D12_RESOURCE_STATE_COMMON;
        }

        const bool IsAnyBufferRecreated{ PersistentResource.mSegmentContextBuffer.Get() != PreviousSegmentContextBuffer || PersistentResource.mPlacementRuleBuffer.Get() != PreviousPlacementRuleBuffer || PersistentResource.mPlacementSpacingRuleRecordBuffer.Get() != PreviousPlacementSpacingRuleRecordBuffer || PersistentResource.mCellMetadataBuffer.Get() != PreviousCellMetadataBuffer || PersistentResource.mAcceptedCandidateBuffer.Get() != PreviousAcceptedCandidateBuffer };
        if (IsAnyBufferRecreated == true) {
            PersistentResource.mUploadedDataHashes = {};
            PersistentResource.mSrvCaches = {};
            PersistentResource.mUavCaches = {};
        }

        return Result;
    }

    bool EnvironmentRuntime::EnsureGpuDrivenFrameResources(EnvironmentGpuDrivenFrameResource& FrameResource, std::uint32_t InstanceContextCount, std::uint32_t SegmentContextCount, std::uint32_t DrawRecordCount, std::uint32_t PlacementConfigCount, std::uint32_t PlacementRuleCount, std::uint32_t PlacementDrawRecordCount, std::uint32_t PlacementDrawDispatchRecordCount, std::uint32_t PlacementCandidateRecordCount, std::uint32_t PlacementCandidateDispatchRecordCount, std::uint32_t PlacementSpacingRuleRecordCount, std::uint32_t PlacementPointAtlasRecordCount, std::uint32_t PlacementPointAtlasPointCount, std::uint32_t CandidateContextCount, std::uint32_t VisibleInstanceIndexCount) {
        if (EnsureGpuDrivenDescriptorHandles(FrameResource) == false) {
            return false;
        }

        const std::uint32_t SafeInstanceContextCount{ std::max(InstanceContextCount, 1u) };
        const std::uint32_t SafeSegmentContextCount{ std::max(SegmentContextCount, 1u) };
        const std::uint32_t SafeDrawRecordCount{ std::max(DrawRecordCount, 1u) };
        const std::uint32_t SafePlacementConfigCount{ std::max(PlacementConfigCount, 1u) };
        const std::uint32_t SafePlacementRuleCount{ std::max(PlacementRuleCount, 1u) };
        const std::uint32_t SafePlacementDrawRecordCount{ std::max(PlacementDrawRecordCount, 1u) };
        const std::uint32_t SafePlacementDrawDispatchRecordCount{ std::max(PlacementDrawDispatchRecordCount, 1u) };
        const std::uint32_t SafePlacementCandidateRecordCount{ std::max(PlacementCandidateRecordCount, 1u) };
        const std::uint32_t SafePlacementCandidateDispatchRecordCount{ std::max(PlacementCandidateDispatchRecordCount, 1u) };
        const std::uint32_t SafePlacementSpacingRuleRecordCount{ std::max(PlacementSpacingRuleRecordCount, 1u) };
        const std::uint32_t SafePlacementPointAtlasRecordCount{ std::max(PlacementPointAtlasRecordCount, 1u) };
        const std::uint32_t SafePlacementPointAtlasPointCount{ std::max(PlacementPointAtlasPointCount, 1u) };
        const std::uint32_t SafeCandidateContextCount{ std::max(CandidateContextCount, 1u) };
        const std::uint32_t SafeVisibleInstanceIndexCount{ std::max(VisibleInstanceIndexCount, 1u) };
        ID3D12Resource* PreviousInstanceContextBuffer{ FrameResource.mInstanceContextBuffer.Get() };
        ID3D12Resource* PreviousSegmentContextBuffer{ FrameResource.mSegmentContextBuffer.Get() };
        ID3D12Resource* PreviousDrawRecordBuffer{ FrameResource.mDrawRecordBuffer.Get() };
        ID3D12Resource* PreviousPlacementConfigBuffer{ FrameResource.mPlacementConfigBuffer.Get() };
        ID3D12Resource* PreviousPlacementRuleBuffer{ FrameResource.mPlacementRuleBuffer.Get() };
        ID3D12Resource* PreviousPlacementDrawRecordBuffer{ FrameResource.mPlacementDrawRecordBuffer.Get() };
        ID3D12Resource* PreviousPlacementDrawDispatchRecordBuffer{ FrameResource.mPlacementDrawDispatchRecordBuffer.Get() };
        ID3D12Resource* PreviousPlacementCandidateRecordBuffer{ FrameResource.mPlacementCandidateRecordBuffer.Get() };
        ID3D12Resource* PreviousPlacementCandidateDispatchRecordBuffer{ FrameResource.mPlacementCandidateDispatchRecordBuffer.Get() };
        ID3D12Resource* PreviousPlacementSpacingRuleRecordBuffer{ FrameResource.mPlacementSpacingRuleRecordBuffer.Get() };
        ID3D12Resource* PreviousPlacementPointAtlasRecordBuffer{ FrameResource.mPlacementPointAtlasRecordBuffer.Get() };
        ID3D12Resource* PreviousPlacementPointAtlasPointBuffer{ FrameResource.mPlacementPointAtlasPointBuffer.Get() };
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
        Result = EnsureGpuDrivenBuffer(FrameResource.mPlacementDrawDispatchRecordBuffer, FrameResource.mPlacementDrawDispatchRecordBufferCapacityInBytes, sizeof(EnvironmentGpuPlacementDrawDispatchRecord) * static_cast<std::uint64_t>(SafePlacementDrawDispatchRecordCount), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.PlacementDrawDispatchRecordBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(FrameResource.mPlacementCandidateRecordBuffer, FrameResource.mPlacementCandidateRecordBufferCapacityInBytes, sizeof(EnvironmentGpuPlacementCandidateRecord) * static_cast<std::uint64_t>(SafePlacementCandidateRecordCount), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.PlacementCandidateRecordBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(FrameResource.mPlacementCandidateDispatchRecordBuffer, FrameResource.mPlacementCandidateDispatchRecordBufferCapacityInBytes, sizeof(EnvironmentGpuPlacementCandidateDispatchRecord) * static_cast<std::uint64_t>(SafePlacementCandidateDispatchRecordCount), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.PlacementCandidateDispatchRecordBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(FrameResource.mPlacementSpacingRuleRecordBuffer, FrameResource.mPlacementSpacingRuleRecordBufferCapacityInBytes, sizeof(EnvironmentGpuPlacementSpacingRuleRecord) * static_cast<std::uint64_t>(SafePlacementSpacingRuleRecordCount), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.PlacementSpacingRuleRecordBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(FrameResource.mPlacementPointAtlasRecordBuffer, FrameResource.mPlacementPointAtlasRecordBufferCapacityInBytes, sizeof(EnvironmentGpuPlacementPointAtlasRecord) * static_cast<std::uint64_t>(SafePlacementPointAtlasRecordCount), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.PlacementPointAtlasRecordBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(FrameResource.mPlacementPointAtlasPointBuffer, FrameResource.mPlacementPointAtlasPointBufferCapacityInBytes, sizeof(EnvironmentGpuPlacementPointAtlasPoint) * static_cast<std::uint64_t>(SafePlacementPointAtlasPointCount), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.PlacementPointAtlasPointBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(FrameResource.mCandidateContextBuffer, FrameResource.mCandidateContextBufferCapacityInBytes, sizeof(EnvironmentGpuPlacementCandidate) * static_cast<std::uint64_t>(SafeCandidateContextCount), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.CandidateContextBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(FrameResource.mVisibleInstanceIndexBuffer, FrameResource.mVisibleInstanceIndexBufferCapacityInBytes, sizeof(std::uint32_t) * static_cast<std::uint64_t>(SafeVisibleInstanceIndexCount), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.VisibleInstanceIndexBuffer") && Result;
        Result = EnsureGpuDrivenBuffer(FrameResource.mIndirectArgumentBuffer, FrameResource.mIndirectArgumentBufferCapacityInBytes, sizeof(EnvironmentIndirectDrawCommand) * static_cast<std::uint64_t>(SafeDrawRecordCount), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, L"EnvironmentRuntime.IndirectCommandBuffer") && Result;
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

        const bool IsAnyBufferRecreated{ FrameResource.mInstanceContextBuffer.Get() != PreviousInstanceContextBuffer || FrameResource.mSegmentContextBuffer.Get() != PreviousSegmentContextBuffer || FrameResource.mDrawRecordBuffer.Get() != PreviousDrawRecordBuffer || FrameResource.mPlacementConfigBuffer.Get() != PreviousPlacementConfigBuffer || FrameResource.mPlacementRuleBuffer.Get() != PreviousPlacementRuleBuffer || FrameResource.mPlacementDrawRecordBuffer.Get() != PreviousPlacementDrawRecordBuffer || FrameResource.mPlacementDrawDispatchRecordBuffer.Get() != PreviousPlacementDrawDispatchRecordBuffer || FrameResource.mPlacementCandidateRecordBuffer.Get() != PreviousPlacementCandidateRecordBuffer || FrameResource.mPlacementCandidateDispatchRecordBuffer.Get() != PreviousPlacementCandidateDispatchRecordBuffer || FrameResource.mPlacementSpacingRuleRecordBuffer.Get() != PreviousPlacementSpacingRuleRecordBuffer || FrameResource.mPlacementPointAtlasRecordBuffer.Get() != PreviousPlacementPointAtlasRecordBuffer || FrameResource.mPlacementPointAtlasPointBuffer.Get() != PreviousPlacementPointAtlasPointBuffer || FrameResource.mCandidateContextBuffer.Get() != PreviousCandidateContextBuffer || FrameResource.mVisibleInstanceIndexBuffer.Get() != PreviousVisibleInstanceIndexBuffer || FrameResource.mIndirectArgumentBuffer.Get() != PreviousIndirectArgumentBuffer };
        if (IsAnyBufferRecreated == true) {
            FrameResource.mUploadedDataHashes = {};
            FrameResource.mSrvCaches = {};
            FrameResource.mUavCaches = {};
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

        const bool NeedDrawBuildItemSort{ std::is_sorted(DrawBuildItems.begin(), DrawBuildItems.end(), [](const EnvironmentGpuDrawBuildItem& Left, const EnvironmentGpuDrawBuildItem& Right) {
            return CompareEnvironmentDrawRecordByPso(Left.mDrawRecord, Right.mDrawRecord);
        }) == false };
        if (NeedDrawBuildItemSort == true) {
            std::sort(DrawBuildItems.begin(), DrawBuildItems.end(), [](const EnvironmentGpuDrawBuildItem& Left, const EnvironmentGpuDrawBuildItem& Right) {
                return CompareEnvironmentDrawRecordByPso(Left.mDrawRecord, Right.mDrawRecord);
            });
        }

        RenderData.mEnvironmentDrawRecords.clear();
        RenderData.mEnvironmentDrawRecords.reserve(DrawBuildItems.size());
        mGpuPlacementFrameData.mDrawRecords.clear();
        mGpuPlacementFrameData.mDrawRecords.reserve(DrawBuildItems.size());
        for (const EnvironmentGpuDrawBuildItem& Item : DrawBuildItems) {
            RenderData.mEnvironmentDrawRecords.push_back(Item.mDrawRecord);
            mGpuPlacementFrameData.mDrawRecords.push_back(Item.mPlacementDrawRecord);
        }

        BuildGpuPlacementDrawDispatchRecords(std::span<const EnvironmentGpuPlacementDrawRecord>{ mGpuPlacementFrameData.mDrawRecords.data(), mGpuPlacementFrameData.mDrawRecords.size() }, mGpuPlacementFrameData.mDrawDispatchRecords);

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
            DestinationRecord.mIndexCountPerInstance = 0u;
            DestinationRecord.mStartIndexLocation = 0u;
            DestinationRecord.mBaseVertexLocation = 0;
            DestinationRecord.mPadding2 = 0u;
            DestinationRecord.mPadding3 = 0u;
            const std::uint64_t InstanceEnd{ static_cast<std::uint64_t>(SourceRecord.mInstanceOffset) + static_cast<std::uint64_t>(SourceRecord.mInstanceCount) };
            mGpuInstanceContextCount = static_cast<std::uint32_t>(std::min<std::uint64_t>(std::max<std::uint64_t>(mGpuInstanceContextCount, InstanceEnd), std::numeric_limits<std::uint32_t>::max()));

            if (SourceRecord.mMesh != nullptr && SourceRecord.mInstanceCount > 0u) {
                const RenderContract::ModelSubMesh& SubMesh{ SourceRecord.mMesh->GetSubMesh(SourceRecord.mSubMesh) };
                DestinationRecord.mIndexCountPerInstance = static_cast<std::uint32_t>(SubMesh.mIndexCount);
                DestinationRecord.mStartIndexLocation = static_cast<std::uint32_t>(SubMesh.mIndexOffset);
            }

            VisibleInstanceOffset += SourceRecord.mInstanceCount;
        }

        OutVisibleInstanceIndexCount = VisibleInstanceOffset;
    }

    RenderContract::Future EnvironmentRuntime::UploadGpuPersistentData(EnvironmentGpuPersistentResource& PersistentResource) {
        if (mCopyQueue == nullptr || PersistentResource.mSegmentContextBuffer == nullptr || PersistentResource.mPlacementRuleBuffer == nullptr || PersistentResource.mPlacementSpacingRuleRecordBuffer == nullptr) {
            return RenderContract::Future{};
        }

        std::vector<Interface::CopyRequest> CopyRequests{};
        CopyRequests.reserve(3ULL);

        auto AppendCopyRequestIfChanged = [&PersistentResource, &CopyRequests](EnvironmentGpuPersistentUploadHashIndex HashIndex, Microsoft::WRL::ComPtr<ID3D12Resource> DestinationResource, std::span<const std::byte> SourceData) {
            if (SourceData.empty() == true || DestinationResource == nullptr) {
                return;
            }

            const std::size_t HashSlot{ static_cast<std::size_t>(HashIndex) };
            const std::uint64_t DataHash{ CalculateEnvironmentGpuDataHash(SourceData) };
            if (PersistentResource.mUploadedDataHashes[HashSlot] == DataHash) {
                return;
            }

            Interface::CopyRequest CopyRequest{ Interface::CopyPriority::High };
            CopyRequest.DestinationDefaultResource = DestinationResource;
            CopyRequest.DestinationOffset = 0u;
            CopyRequest.SourceData = SourceData;
            CopyRequests.push_back(CopyRequest);
            PersistentResource.mUploadedDataHashes[HashSlot] = DataHash;
        };

        AppendCopyRequestIfChanged(EnvironmentGpuPersistentUploadHashIndex::SegmentContexts, PersistentResource.mSegmentContextBuffer, MakeVectorByteSpan(mGpuSegmentContexts));
        AppendCopyRequestIfChanged(EnvironmentGpuPersistentUploadHashIndex::PlacementRules, PersistentResource.mPlacementRuleBuffer, MakeVectorByteSpan(mGpuPlacementFrameData.mRules));
        AppendCopyRequestIfChanged(EnvironmentGpuPersistentUploadHashIndex::PlacementSpacingRuleRecords, PersistentResource.mPlacementSpacingRuleRecordBuffer, MakeVectorByteSpan(mGpuPlacementFrameData.mSpacingRuleRecords));

        RenderContract::Future CopyFuture{ mCopyQueue->EnqueueCopyFuture(CopyRequests) };
        mCopyQueue->DispatchCopies();
        return CopyFuture;
    }

    RenderContract::Future EnvironmentRuntime::UploadGpuDrivenFrameData(EnvironmentGpuDrivenFrameResource& FrameResource) {
        if (mCopyQueue == nullptr || FrameResource.mDrawRecordBuffer == nullptr || FrameResource.mPlacementConfigBuffer == nullptr || FrameResource.mPlacementDrawRecordBuffer == nullptr || FrameResource.mPlacementDrawDispatchRecordBuffer == nullptr || FrameResource.mPlacementCandidateRecordBuffer == nullptr || FrameResource.mPlacementCandidateDispatchRecordBuffer == nullptr || FrameResource.mPlacementPointAtlasRecordBuffer == nullptr || FrameResource.mPlacementPointAtlasPointBuffer == nullptr) {
            return RenderContract::Future{};
        }

        std::vector<Interface::CopyRequest> CopyRequests{};
        CopyRequests.reserve(8ULL);

        auto AppendCopyRequestIfChanged = [&FrameResource, &CopyRequests](EnvironmentGpuUploadHashIndex HashIndex, Microsoft::WRL::ComPtr<ID3D12Resource> DestinationResource, std::span<const std::byte> SourceData) {
            if (SourceData.empty() == true || DestinationResource == nullptr) {
                return;
            }

            const std::size_t HashSlot{ static_cast<std::size_t>(HashIndex) };
            const std::uint64_t DataHash{ CalculateEnvironmentGpuDataHash(SourceData) };
            if (FrameResource.mUploadedDataHashes[HashSlot] == DataHash) {
                return;
            }

            Interface::CopyRequest CopyRequest{ Interface::CopyPriority::High };
            CopyRequest.DestinationDefaultResource = DestinationResource;
            CopyRequest.DestinationOffset = 0u;
            CopyRequest.SourceData = SourceData;
            CopyRequests.push_back(CopyRequest);
            FrameResource.mUploadedDataHashes[HashSlot] = DataHash;
        };

        AppendCopyRequestIfChanged(EnvironmentGpuUploadHashIndex::DrawRecords, FrameResource.mDrawRecordBuffer, MakeVectorByteSpan(mGpuDrawRecords));
        AppendCopyRequestIfChanged(EnvironmentGpuUploadHashIndex::PlacementConfig, FrameResource.mPlacementConfigBuffer, MakeByteSpan(&mGpuPlacementFrameData.mConfig, sizeof(EnvironmentGpuPlacementConfig)));
        AppendCopyRequestIfChanged(EnvironmentGpuUploadHashIndex::PlacementDrawRecords, FrameResource.mPlacementDrawRecordBuffer, MakeVectorByteSpan(mGpuPlacementFrameData.mDrawRecords));
        AppendCopyRequestIfChanged(EnvironmentGpuUploadHashIndex::PlacementDrawDispatchRecords, FrameResource.mPlacementDrawDispatchRecordBuffer, MakeVectorByteSpan(mGpuPlacementFrameData.mDrawDispatchRecords));
        AppendCopyRequestIfChanged(EnvironmentGpuUploadHashIndex::PlacementCandidateRecords, FrameResource.mPlacementCandidateRecordBuffer, MakeVectorByteSpan(mGpuPlacementFrameData.mCandidateRecords));
        AppendCopyRequestIfChanged(EnvironmentGpuUploadHashIndex::PlacementCandidateDispatchRecords, FrameResource.mPlacementCandidateDispatchRecordBuffer, MakeVectorByteSpan(mGpuPlacementFrameData.mCandidateDispatchRecords));
        AppendCopyRequestIfChanged(EnvironmentGpuUploadHashIndex::PlacementPointAtlasRecords, FrameResource.mPlacementPointAtlasRecordBuffer, MakeVectorByteSpan(mGpuPlacementFrameData.mPointAtlasRecords));
        AppendCopyRequestIfChanged(EnvironmentGpuUploadHashIndex::PlacementPointAtlasPoints, FrameResource.mPlacementPointAtlasPointBuffer, MakeVectorByteSpan(mGpuPlacementFrameData.mPointAtlasPoints));

        RenderContract::Future CopyFuture{ mCopyQueue->EnqueueCopyFuture(CopyRequests) };
        mCopyQueue->DispatchCopies();
        return CopyFuture;
    }

    RenderContract::Future EnvironmentRuntime::DispatchGpuDrivenFrame(EnvironmentGpuDrivenFrameResource& FrameResource, const EnvironmentFrameInput& Input, const RenderContract::Future& CopyFuture, std::uint32_t DrawRecordCount, std::uint32_t VisibleInstanceIndexCapacity, std::uint32_t CandidateRecordCount, std::uint32_t CandidateDispatchRecordCount, std::uint32_t DenseCandidateDispatchRecordCount, std::uint32_t SpacedCandidateDispatchRecordCount, std::uint32_t DrawDispatchRecordCount, std::uint32_t SpacingRuleRecordCount) {
        if (mComputeQueue == nullptr || mSrvHeap == nullptr || DrawRecordCount == 0u || CandidateRecordCount == 0u || DrawDispatchRecordCount == 0u || mGpuStatusUavIndex == InvalidDescriptorIndex || mIndirectCommandInitializePipelineState == nullptr || mDenseCandidateGeneratePipelineState == nullptr || mSpacedCandidateGeneratePipelineState == nullptr || mCandidateClassifyPipelineState == nullptr || FrameResource.mInstanceContextSrvHandle.IsValid() == false || FrameResource.mInstanceContextUavHandle.IsValid() == false || FrameResource.mDrawRecordSrvHandle.IsValid() == false || FrameResource.mPlacementConfigSrvHandle.IsValid() == false || FrameResource.mPlacementDrawRecordSrvHandle.IsValid() == false || FrameResource.mPlacementDrawDispatchRecordSrvHandle.IsValid() == false || FrameResource.mPlacementCandidateRecordSrvHandle.IsValid() == false || FrameResource.mPlacementCandidateDispatchRecordSrvHandle.IsValid() == false || FrameResource.mPlacementPointAtlasRecordSrvHandle.IsValid() == false || FrameResource.mPlacementPointAtlasPointSrvHandle.IsValid() == false || FrameResource.mCandidateContextSrvHandle.IsValid() == false || FrameResource.mCandidateContextUavHandle.IsValid() == false || FrameResource.mIndirectArgumentUavHandle.IsValid() == false || FrameResource.mVisibleInstanceIndexUavHandle.IsValid() == false || mGpuPersistentResource.mSegmentContextSrvHandle.IsValid() == false || mGpuPersistentResource.mPlacementRuleSrvHandle.IsValid() == false || mGpuPersistentResource.mPlacementSpacingRuleRecordSrvHandle.IsValid() == false || mGpuPersistentResource.mCellMetadataSrvHandle.IsValid() == false || mGpuPersistentResource.mCellMetadataUavHandle.IsValid() == false || mGpuPersistentResource.mAcceptedCandidateSrvHandle.IsValid() == false || mGpuPersistentResource.mAcceptedCandidateUavHandle.IsValid() == false) {
            return RenderContract::Future{};
        }

        Microsoft::WRL::ComPtr<ID3D12Resource> InstanceContextBuffer{ FrameResource.mInstanceContextBuffer };
        Microsoft::WRL::ComPtr<ID3D12Resource> CandidateContextBuffer{ FrameResource.mCandidateContextBuffer };
        Microsoft::WRL::ComPtr<ID3D12Resource> VisibleInstanceIndexBuffer{ FrameResource.mVisibleInstanceIndexBuffer };
        Microsoft::WRL::ComPtr<ID3D12Resource> IndirectArgumentBuffer{ FrameResource.mIndirectArgumentBuffer };
        Microsoft::WRL::ComPtr<ID3D12Resource> CellMetadataBuffer{ mGpuPersistentResource.mCellMetadataBuffer };
        Microsoft::WRL::ComPtr<ID3D12Resource> AcceptedCandidateBuffer{ mGpuPersistentResource.mAcceptedCandidateBuffer };
        const D3D12_RESOURCE_STATES InstanceContextStateBeforeDispatch{ FrameResource.mInstanceContextState };
        const D3D12_RESOURCE_STATES CandidateContextStateBeforeDispatch{ FrameResource.mCandidateContextState };
        const D3D12_RESOURCE_STATES VisibleInstanceIndexStateBeforeDispatch{ FrameResource.mVisibleInstanceIndexState };
        const D3D12_RESOURCE_STATES IndirectArgumentStateBeforeDispatch{ FrameResource.mIndirectArgumentState };
        const D3D12_RESOURCE_STATES CellMetadataStateBeforeDispatch{ mGpuPersistentResource.mCellMetadataState };
        const D3D12_RESOURCE_STATES AcceptedCandidateStateBeforeDispatch{ mGpuPersistentResource.mAcceptedCandidateState };
        const EnvironmentGpuRootConstants RootConstants{ BuildEnvironmentGpuRootConstants(Input, mGpuStatusUavIndex, FrameResource.mInstanceContextSrvHandle.GetIndex(), FrameResource.mInstanceContextUavHandle.GetIndex(), FrameResource.mDrawRecordSrvHandle.GetIndex(), FrameResource.mPlacementConfigSrvHandle.GetIndex(), mGpuPersistentResource.mPlacementRuleSrvHandle.GetIndex(), FrameResource.mPlacementDrawRecordSrvHandle.GetIndex(), FrameResource.mPlacementCandidateRecordSrvHandle.GetIndex(), FrameResource.mPlacementCandidateDispatchRecordSrvHandle.GetIndex(), FrameResource.mPlacementPointAtlasRecordSrvHandle.GetIndex(), FrameResource.mPlacementPointAtlasPointSrvHandle.GetIndex(), FrameResource.mPlacementDrawDispatchRecordSrvHandle.GetIndex(), FrameResource.mCandidateContextSrvHandle.GetIndex(), FrameResource.mCandidateContextUavHandle.GetIndex(), FrameResource.mIndirectArgumentUavHandle.GetIndex(), FrameResource.mVisibleInstanceIndexUavHandle.GetIndex(), mGpuPersistentResource.mCellMetadataSrvHandle.GetIndex(), mGpuPersistentResource.mCellMetadataUavHandle.GetIndex(), mGpuPersistentResource.mAcceptedCandidateSrvHandle.GetIndex(), mGpuPersistentResource.mAcceptedCandidateUavHandle.GetIndex(), DrawRecordCount, VisibleInstanceIndexCapacity, CandidateRecordCount, CandidateDispatchRecordCount, DrawDispatchRecordCount, SpacingRuleRecordCount) };
        EnvironmentGpuRootConstants DenseGenerateRootConstants{ RootConstants };
        DenseGenerateRootConstants.mCandidateDispatchRecordCount = DenseCandidateDispatchRecordCount;
        DenseGenerateRootConstants.mCandidateDispatchRecordOffset = 0u;
        EnvironmentGpuRootConstants SpacedGenerateRootConstants{ RootConstants };
        SpacedGenerateRootConstants.mCandidateDispatchRecordCount = SpacedCandidateDispatchRecordCount;
        SpacedGenerateRootConstants.mCandidateDispatchRecordOffset = DenseCandidateDispatchRecordCount;

        const std::array<RenderContract::Future, 2> WaitFutures{ CopyFuture, Input.mTerrain.mUploadFuture };
        Interface::ComputeQueueDispatchRequest InitializeRequest{};
        InitializeRequest.WaitFuture = RenderContract::Future::Merge(WaitFutures);
        InitializeRequest.RootSignature = mComputeRootSignature;
        InitializeRequest.PipelineState = mIndirectCommandInitializePipelineState;
        InitializeRequest.DescriptorHeaps = std::vector<ID3D12DescriptorHeap*>{ mSrvHeap->GetHeap() };
        InitializeRequest.RecordCommands = [IndirectArgumentBuffer, RootConstants, IndirectArgumentStateBeforeDispatch](ID3D12GraphicsCommandList* CommandList) {
            if (CommandList == nullptr) {
                return;
            }

            if (IndirectArgumentBuffer != nullptr && IndirectArgumentStateBeforeDispatch != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
                D3D12_RESOURCE_BARRIER Barrier{};
                Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                Barrier.Transition.pResource = IndirectArgumentBuffer.Get();
                Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                Barrier.Transition.StateBefore = IndirectArgumentStateBeforeDispatch;
                Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                CommandList->ResourceBarrier(1u, &Barrier);
            }

            CommandList->SetComputeRoot32BitConstants(0, EnvironmentGpuRootConstantDwordCount, &RootConstants, 0);
        };
        InitializeRequest.ThreadGroupCountX = CalculateEnvironmentDispatchGroupCount(DrawRecordCount);
        InitializeRequest.ThreadGroupCountY = 1u;
        InitializeRequest.ThreadGroupCountZ = 1u;

        Interface::ComputeQueueDispatchRequest DenseGenerateRequest{};
        DenseGenerateRequest.RootSignature = mComputeRootSignature;
        DenseGenerateRequest.PipelineState = mDenseCandidateGeneratePipelineState;
        DenseGenerateRequest.DescriptorHeaps = std::vector<ID3D12DescriptorHeap*>{ mSrvHeap->GetHeap() };
        DenseGenerateRequest.RecordCommands = [CandidateContextBuffer, CellMetadataBuffer, AcceptedCandidateBuffer, DenseGenerateRootConstants, CandidateContextStateBeforeDispatch, CellMetadataStateBeforeDispatch, AcceptedCandidateStateBeforeDispatch](ID3D12GraphicsCommandList* CommandList) {
            if (CommandList == nullptr) {
                return;
            }

            std::array<D3D12_RESOURCE_BARRIER, 3> Barriers{};
            std::uint32_t BarrierCount{};
            if (CandidateContextBuffer != nullptr && CandidateContextStateBeforeDispatch != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
                D3D12_RESOURCE_BARRIER& Barrier{ Barriers[BarrierCount] };
                Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                Barrier.Transition.pResource = CandidateContextBuffer.Get();
                Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                Barrier.Transition.StateBefore = CandidateContextStateBeforeDispatch;
                Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                BarrierCount += 1u;
            }

            if (CellMetadataBuffer != nullptr && CellMetadataStateBeforeDispatch != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
                D3D12_RESOURCE_BARRIER& Barrier{ Barriers[BarrierCount] };
                Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                Barrier.Transition.pResource = CellMetadataBuffer.Get();
                Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                Barrier.Transition.StateBefore = CellMetadataStateBeforeDispatch;
                Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                BarrierCount += 1u;
            }

            if (AcceptedCandidateBuffer != nullptr && AcceptedCandidateStateBeforeDispatch != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
                D3D12_RESOURCE_BARRIER& Barrier{ Barriers[BarrierCount] };
                Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                Barrier.Transition.pResource = AcceptedCandidateBuffer.Get();
                Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                Barrier.Transition.StateBefore = AcceptedCandidateStateBeforeDispatch;
                Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                BarrierCount += 1u;
            }

            if (BarrierCount > 0u) {
                CommandList->ResourceBarrier(BarrierCount, Barriers.data());
            }

            CommandList->SetComputeRoot32BitConstants(0, EnvironmentGpuRootConstantDwordCount, &DenseGenerateRootConstants, 0);
        };
        DenseGenerateRequest.ThreadGroupCountX = std::max(DenseCandidateDispatchRecordCount, 1u);
        DenseGenerateRequest.ThreadGroupCountY = 1u;
        DenseGenerateRequest.ThreadGroupCountZ = 1u;

        Interface::ComputeQueueDispatchRequest SpacedGenerateRequest{};
        SpacedGenerateRequest.RootSignature = mComputeRootSignature;
        SpacedGenerateRequest.PipelineState = mSpacedCandidateGeneratePipelineState;
        SpacedGenerateRequest.DescriptorHeaps = std::vector<ID3D12DescriptorHeap*>{ mSrvHeap->GetHeap() };
        SpacedGenerateRequest.RecordCommands = [CandidateContextBuffer, CellMetadataBuffer, AcceptedCandidateBuffer, SpacedGenerateRootConstants](ID3D12GraphicsCommandList* CommandList) {
            if (CommandList == nullptr) {
                return;
            }

            std::array<D3D12_RESOURCE_BARRIER, 3> Barriers{};
            std::uint32_t BarrierCount{};
            if (CandidateContextBuffer != nullptr) {
                D3D12_RESOURCE_BARRIER& Barrier{ Barriers[BarrierCount] };
                Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                Barrier.UAV.pResource = CandidateContextBuffer.Get();
                BarrierCount += 1u;
            }

            if (CellMetadataBuffer != nullptr) {
                D3D12_RESOURCE_BARRIER& Barrier{ Barriers[BarrierCount] };
                Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                Barrier.UAV.pResource = CellMetadataBuffer.Get();
                BarrierCount += 1u;
            }

            if (AcceptedCandidateBuffer != nullptr) {
                D3D12_RESOURCE_BARRIER& Barrier{ Barriers[BarrierCount] };
                Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                Barrier.UAV.pResource = AcceptedCandidateBuffer.Get();
                BarrierCount += 1u;
            }

            if (BarrierCount > 0u) {
                CommandList->ResourceBarrier(BarrierCount, Barriers.data());
            }

            CommandList->SetComputeRoot32BitConstants(0, EnvironmentGpuRootConstantDwordCount, &SpacedGenerateRootConstants, 0);
        };
        SpacedGenerateRequest.ThreadGroupCountX = std::max(SpacedCandidateDispatchRecordCount, 1u);
        SpacedGenerateRequest.ThreadGroupCountY = 1u;
        SpacedGenerateRequest.ThreadGroupCountZ = 1u;

        Interface::ComputeQueueDispatchRequest ClassifyRequest{};
        ClassifyRequest.RootSignature = mComputeRootSignature;
        ClassifyRequest.PipelineState = mCandidateClassifyPipelineState;
        ClassifyRequest.DescriptorHeaps = std::vector<ID3D12DescriptorHeap*>{ mSrvHeap->GetHeap() };
        ClassifyRequest.RecordCommands = [InstanceContextBuffer, CandidateContextBuffer, AcceptedCandidateBuffer, VisibleInstanceIndexBuffer, IndirectArgumentBuffer, RootConstants, InstanceContextStateBeforeDispatch, VisibleInstanceIndexStateBeforeDispatch](ID3D12GraphicsCommandList* CommandList) {
            if (CommandList == nullptr) {
                return;
            }

            std::array<D3D12_RESOURCE_BARRIER, 6> Barriers{};
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

            if (AcceptedCandidateBuffer != nullptr) {
                D3D12_RESOURCE_BARRIER& Barrier{ Barriers[BarrierCount] };
                Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                Barrier.Transition.pResource = AcceptedCandidateBuffer.Get();
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

            if (IndirectArgumentBuffer != nullptr) {
                D3D12_RESOURCE_BARRIER& Barrier{ Barriers[BarrierCount] };
                Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                Barrier.UAV.pResource = IndirectArgumentBuffer.Get();
                BarrierCount += 1u;
            }

            if (BarrierCount > 0u) {
                CommandList->ResourceBarrier(BarrierCount, Barriers.data());
            }

            CommandList->SetComputeRoot32BitConstants(0, EnvironmentGpuRootConstantDwordCount, &RootConstants, 0);
        };
        ClassifyRequest.ThreadGroupCountX = DrawDispatchRecordCount;
        ClassifyRequest.ThreadGroupCountY = 1u;
        ClassifyRequest.ThreadGroupCountZ = 1u;

        const std::array<Interface::ComputeQueueDispatchRequest, 4> DispatchRequests{ InitializeRequest, DenseGenerateRequest, SpacedGenerateRequest, ClassifyRequest };
        RenderContract::Future GpuFuture{ mComputeQueue->EnqueueComputeFuture(std::span<const Interface::ComputeQueueDispatchRequest>{ DispatchRequests.data(), DispatchRequests.size() }) };
        mComputeQueue->DispatchComputes();
        FrameResource.mInstanceContextState = D3D12_RESOURCE_STATE_COMMON;
        FrameResource.mCandidateContextState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        FrameResource.mVisibleInstanceIndexState = D3D12_RESOURCE_STATE_COMMON;
        FrameResource.mIndirectArgumentState = D3D12_RESOURCE_STATE_COMMON;
        mGpuPersistentResource.mCellMetadataState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        mGpuPersistentResource.mAcceptedCandidateState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        return GpuFuture;
    }

    void EnvironmentRuntime::FillGpuDrivenFramePayload(EnvironmentGpuDrivenFrameResource& FrameResource, RenderContract::RenderFrameData& RenderData, const RenderContract::Future& GpuDispatchFuture) {
        RenderContract::EnvironmentGpuDrivenFrameData Payload{};
        Payload.mGpuDispatchFuture = GpuDispatchFuture;
        Payload.mInstanceContextResource = FrameResource.mInstanceContextBuffer.Get();
        Payload.mSegmentContextResource = mGpuPersistentResource.mSegmentContextBuffer.Get();
        Payload.mDrawRecordResource = FrameResource.mDrawRecordBuffer.Get();
        Payload.mVisibleInstanceIndexResource = FrameResource.mVisibleInstanceIndexBuffer.Get();
        Payload.mIndirectArgumentResource = FrameResource.mIndirectArgumentBuffer.Get();
        Payload.mInstanceContextSrvIndex = FrameResource.mInstanceContextSrvHandle.GetIndex();
        Payload.mSegmentContextSrvIndex = mGpuPersistentResource.mSegmentContextSrvHandle.GetIndex();
        Payload.mDrawRecordSrvIndex = FrameResource.mDrawRecordSrvHandle.GetIndex();
        Payload.mVisibleInstanceIndexSrvIndex = FrameResource.mVisibleInstanceIndexSrvHandle.GetIndex();
        Payload.mDrawRecordCount = static_cast<std::uint32_t>(mGpuDrawRecords.size());
        Payload.mInstanceContextCount = mGpuInstanceContextCount;
        BuildEnvironmentGpuDrivenGBufferDrawBatches(std::span<const RenderContract::EnvironmentDrawRecord>{ RenderData.mEnvironmentDrawRecords.data(), RenderData.mEnvironmentDrawRecords.size() }, Payload.mGBufferDrawBatches);
        BuildEnvironmentGpuDrivenShadowDrawBatches(std::span<const RenderContract::EnvironmentDrawRecord>{ RenderData.mEnvironmentDrawRecords.data(), RenderData.mEnvironmentDrawRecords.size() }, RenderData.mShadowMappingParameter, Payload.mShadowDrawBatches);
        Payload.mEnabled = GpuDispatchFuture.IsValid() == true && Payload.mInstanceContextResource != nullptr && Payload.mSegmentContextResource != nullptr && Payload.mDrawRecordResource != nullptr && Payload.mVisibleInstanceIndexResource != nullptr && Payload.mIndirectArgumentResource != nullptr;
        RenderData.mEnvironmentGpuDrivenFrame = std::move(Payload);
    }

    void EnvironmentRuntime::UpdateGpuPersistentShaderResourceViews(EnvironmentGpuPersistentResource& PersistentResource, std::uint32_t SegmentContextCount, std::uint32_t PlacementRuleCount, std::uint32_t PlacementSpacingRuleRecordCount, std::uint32_t CellMetadataCount, std::uint32_t AcceptedCandidateCount) {
        if (mDevice == nullptr) {
            return;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC SrvDesc{};
        SrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        SrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        SrvDesc.Format = DXGI_FORMAT_UNKNOWN;
        SrvDesc.Buffer.FirstElement = 0u;
        SrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        auto CreateSrvIfChanged = [this, &PersistentResource, &SrvDesc](EnvironmentGpuPersistentSrvCacheIndex CacheIndex, ID3D12Resource* Resource, D3D12_CPU_DESCRIPTOR_HANDLE Handle, std::uint32_t ElementCount, std::uint32_t Stride) {
            EnvironmentGpuDescriptorCache& Cache{ PersistentResource.mSrvCaches[static_cast<std::size_t>(CacheIndex)] };
            if (IsEnvironmentGpuDescriptorCacheValid(Cache, Resource, ElementCount, Stride) == true) {
                return;
            }

            SrvDesc.Buffer.NumElements = ElementCount;
            SrvDesc.Buffer.StructureByteStride = Stride;
            mDevice->CreateShaderResourceView(Resource, &SrvDesc, Handle);
            Cache.mResource = Resource;
            Cache.mElementCount = ElementCount;
            Cache.mStride = Stride;
        };

        CreateSrvIfChanged(EnvironmentGpuPersistentSrvCacheIndex::SegmentContext, PersistentResource.mSegmentContextBuffer.Get(), PersistentResource.mSegmentContextSrvHandle.GetCPU(), std::max(SegmentContextCount, 1u), sizeof(RenderContract::EnvironmentSegmentContext));
        CreateSrvIfChanged(EnvironmentGpuPersistentSrvCacheIndex::PlacementRule, PersistentResource.mPlacementRuleBuffer.Get(), PersistentResource.mPlacementRuleSrvHandle.GetCPU(), std::max(PlacementRuleCount, 1u), sizeof(EnvironmentGpuPlacementRule));
        CreateSrvIfChanged(EnvironmentGpuPersistentSrvCacheIndex::PlacementSpacingRuleRecord, PersistentResource.mPlacementSpacingRuleRecordBuffer.Get(), PersistentResource.mPlacementSpacingRuleRecordSrvHandle.GetCPU(), std::max(PlacementSpacingRuleRecordCount, 1u), sizeof(EnvironmentGpuPlacementSpacingRuleRecord));
        CreateSrvIfChanged(EnvironmentGpuPersistentSrvCacheIndex::CellMetadata, PersistentResource.mCellMetadataBuffer.Get(), PersistentResource.mCellMetadataSrvHandle.GetCPU(), std::max(CellMetadataCount, 1u), sizeof(EnvironmentGpuPlacementCellMetadata));
        CreateSrvIfChanged(EnvironmentGpuPersistentSrvCacheIndex::AcceptedCandidate, PersistentResource.mAcceptedCandidateBuffer.Get(), PersistentResource.mAcceptedCandidateSrvHandle.GetCPU(), std::max(AcceptedCandidateCount, 1u), sizeof(EnvironmentGpuPlacementCandidate));
    }

    void EnvironmentRuntime::UpdateGpuPersistentUnorderedAccessViews(EnvironmentGpuPersistentResource& PersistentResource, std::uint32_t CellMetadataCount, std::uint32_t AcceptedCandidateCount) {
        if (mDevice == nullptr) {
            return;
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC UavDesc{};
        UavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        UavDesc.Format = DXGI_FORMAT_UNKNOWN;
        UavDesc.Buffer.FirstElement = 0u;
        UavDesc.Buffer.CounterOffsetInBytes = 0u;
        UavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

        auto CreateUavIfChanged = [this, &PersistentResource, &UavDesc](EnvironmentGpuPersistentUavCacheIndex CacheIndex, ID3D12Resource* Resource, D3D12_CPU_DESCRIPTOR_HANDLE Handle, std::uint32_t ElementCount, std::uint32_t Stride) {
            EnvironmentGpuDescriptorCache& Cache{ PersistentResource.mUavCaches[static_cast<std::size_t>(CacheIndex)] };
            if (IsEnvironmentGpuDescriptorCacheValid(Cache, Resource, ElementCount, Stride) == true) {
                return;
            }

            UavDesc.Buffer.NumElements = ElementCount;
            UavDesc.Buffer.StructureByteStride = Stride;
            mDevice->CreateUnorderedAccessView(Resource, nullptr, &UavDesc, Handle);
            Cache.mResource = Resource;
            Cache.mElementCount = ElementCount;
            Cache.mStride = Stride;
        };

        CreateUavIfChanged(EnvironmentGpuPersistentUavCacheIndex::CellMetadata, PersistentResource.mCellMetadataBuffer.Get(), PersistentResource.mCellMetadataUavHandle.GetCPU(), std::max(CellMetadataCount, 1u), sizeof(EnvironmentGpuPlacementCellMetadata));
        CreateUavIfChanged(EnvironmentGpuPersistentUavCacheIndex::AcceptedCandidate, PersistentResource.mAcceptedCandidateBuffer.Get(), PersistentResource.mAcceptedCandidateUavHandle.GetCPU(), std::max(AcceptedCandidateCount, 1u), sizeof(EnvironmentGpuPlacementCandidate));
    }

    void EnvironmentRuntime::UpdateGpuDrivenShaderResourceViews(EnvironmentGpuDrivenFrameResource& FrameResource, std::uint32_t InstanceContextCount, std::uint32_t SegmentContextCount, std::uint32_t DrawRecordCount, std::uint32_t PlacementConfigCount, std::uint32_t PlacementRuleCount, std::uint32_t PlacementDrawRecordCount, std::uint32_t PlacementDrawDispatchRecordCount, std::uint32_t PlacementCandidateRecordCount, std::uint32_t PlacementCandidateDispatchRecordCount, std::uint32_t PlacementSpacingRuleRecordCount, std::uint32_t PlacementPointAtlasRecordCount, std::uint32_t PlacementPointAtlasPointCount, std::uint32_t CandidateContextCount, std::uint32_t VisibleInstanceIndexCount) {
        if (mDevice == nullptr) {
            return;
        }

        const std::uint32_t SafeInstanceContextCount{ std::max(InstanceContextCount, 1u) };
        const std::uint32_t SafeSegmentContextCount{ std::max(SegmentContextCount, 1u) };
        const std::uint32_t SafeDrawRecordCount{ std::max(DrawRecordCount, 1u) };
        const std::uint32_t SafePlacementConfigCount{ std::max(PlacementConfigCount, 1u) };
        const std::uint32_t SafePlacementRuleCount{ std::max(PlacementRuleCount, 1u) };
        const std::uint32_t SafePlacementDrawRecordCount{ std::max(PlacementDrawRecordCount, 1u) };
        const std::uint32_t SafePlacementDrawDispatchRecordCount{ std::max(PlacementDrawDispatchRecordCount, 1u) };
        const std::uint32_t SafePlacementCandidateRecordCount{ std::max(PlacementCandidateRecordCount, 1u) };
        const std::uint32_t SafePlacementCandidateDispatchRecordCount{ std::max(PlacementCandidateDispatchRecordCount, 1u) };
        const std::uint32_t SafePlacementSpacingRuleRecordCount{ std::max(PlacementSpacingRuleRecordCount, 1u) };
        const std::uint32_t SafePlacementPointAtlasRecordCount{ std::max(PlacementPointAtlasRecordCount, 1u) };
        const std::uint32_t SafePlacementPointAtlasPointCount{ std::max(PlacementPointAtlasPointCount, 1u) };
        const std::uint32_t SafeCandidateContextCount{ std::max(CandidateContextCount, 1u) };
        const std::uint32_t SafeVisibleInstanceIndexCount{ std::max(VisibleInstanceIndexCount, 1u) };

        D3D12_SHADER_RESOURCE_VIEW_DESC SrvDesc{};
        SrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        SrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        SrvDesc.Format = DXGI_FORMAT_UNKNOWN;
        SrvDesc.Buffer.FirstElement = 0u;
        SrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        auto CreateSrvIfChanged = [this, &FrameResource, &SrvDesc](EnvironmentGpuSrvCacheIndex CacheIndex, ID3D12Resource* Resource, D3D12_CPU_DESCRIPTOR_HANDLE Handle, std::uint32_t ElementCount, std::uint32_t Stride) {
            EnvironmentGpuDescriptorCache& Cache{ FrameResource.mSrvCaches[static_cast<std::size_t>(CacheIndex)] };
            if (IsEnvironmentGpuDescriptorCacheValid(Cache, Resource, ElementCount, Stride) == true) {
                return;
            }

            SrvDesc.Buffer.NumElements = ElementCount;
            SrvDesc.Buffer.StructureByteStride = Stride;
            mDevice->CreateShaderResourceView(Resource, &SrvDesc, Handle);
            Cache.mResource = Resource;
            Cache.mElementCount = ElementCount;
            Cache.mStride = Stride;
        };

        CreateSrvIfChanged(EnvironmentGpuSrvCacheIndex::InstanceContext, FrameResource.mInstanceContextBuffer.Get(), FrameResource.mInstanceContextSrvHandle.GetCPU(), SafeInstanceContextCount, sizeof(RenderContract::EnvironmentInstanceContext));
        CreateSrvIfChanged(EnvironmentGpuSrvCacheIndex::SegmentContext, FrameResource.mSegmentContextBuffer.Get(), FrameResource.mSegmentContextSrvHandle.GetCPU(), SafeSegmentContextCount, sizeof(RenderContract::EnvironmentSegmentContext));
        CreateSrvIfChanged(EnvironmentGpuSrvCacheIndex::DrawRecord, FrameResource.mDrawRecordBuffer.Get(), FrameResource.mDrawRecordSrvHandle.GetCPU(), SafeDrawRecordCount, sizeof(RenderContract::EnvironmentDrawRecordGpu));
        CreateSrvIfChanged(EnvironmentGpuSrvCacheIndex::PlacementConfig, FrameResource.mPlacementConfigBuffer.Get(), FrameResource.mPlacementConfigSrvHandle.GetCPU(), SafePlacementConfigCount, sizeof(EnvironmentGpuPlacementConfig));
        CreateSrvIfChanged(EnvironmentGpuSrvCacheIndex::PlacementRule, FrameResource.mPlacementRuleBuffer.Get(), FrameResource.mPlacementRuleSrvHandle.GetCPU(), SafePlacementRuleCount, sizeof(EnvironmentGpuPlacementRule));
        CreateSrvIfChanged(EnvironmentGpuSrvCacheIndex::PlacementDrawRecord, FrameResource.mPlacementDrawRecordBuffer.Get(), FrameResource.mPlacementDrawRecordSrvHandle.GetCPU(), SafePlacementDrawRecordCount, sizeof(EnvironmentGpuPlacementDrawRecord));
        CreateSrvIfChanged(EnvironmentGpuSrvCacheIndex::PlacementDrawDispatchRecord, FrameResource.mPlacementDrawDispatchRecordBuffer.Get(), FrameResource.mPlacementDrawDispatchRecordSrvHandle.GetCPU(), SafePlacementDrawDispatchRecordCount, sizeof(EnvironmentGpuPlacementDrawDispatchRecord));
        CreateSrvIfChanged(EnvironmentGpuSrvCacheIndex::PlacementCandidateRecord, FrameResource.mPlacementCandidateRecordBuffer.Get(), FrameResource.mPlacementCandidateRecordSrvHandle.GetCPU(), SafePlacementCandidateRecordCount, sizeof(EnvironmentGpuPlacementCandidateRecord));
        CreateSrvIfChanged(EnvironmentGpuSrvCacheIndex::PlacementCandidateDispatchRecord, FrameResource.mPlacementCandidateDispatchRecordBuffer.Get(), FrameResource.mPlacementCandidateDispatchRecordSrvHandle.GetCPU(), SafePlacementCandidateDispatchRecordCount, sizeof(EnvironmentGpuPlacementCandidateDispatchRecord));
        CreateSrvIfChanged(EnvironmentGpuSrvCacheIndex::PlacementSpacingRuleRecord, FrameResource.mPlacementSpacingRuleRecordBuffer.Get(), FrameResource.mPlacementSpacingRuleRecordSrvHandle.GetCPU(), SafePlacementSpacingRuleRecordCount, sizeof(EnvironmentGpuPlacementSpacingRuleRecord));
        CreateSrvIfChanged(EnvironmentGpuSrvCacheIndex::PlacementPointAtlasRecord, FrameResource.mPlacementPointAtlasRecordBuffer.Get(), FrameResource.mPlacementPointAtlasRecordSrvHandle.GetCPU(), SafePlacementPointAtlasRecordCount, sizeof(EnvironmentGpuPlacementPointAtlasRecord));
        CreateSrvIfChanged(EnvironmentGpuSrvCacheIndex::PlacementPointAtlasPoint, FrameResource.mPlacementPointAtlasPointBuffer.Get(), FrameResource.mPlacementPointAtlasPointSrvHandle.GetCPU(), SafePlacementPointAtlasPointCount, sizeof(EnvironmentGpuPlacementPointAtlasPoint));
        CreateSrvIfChanged(EnvironmentGpuSrvCacheIndex::CandidateContext, FrameResource.mCandidateContextBuffer.Get(), FrameResource.mCandidateContextSrvHandle.GetCPU(), SafeCandidateContextCount, sizeof(EnvironmentGpuPlacementCandidate));
        CreateSrvIfChanged(EnvironmentGpuSrvCacheIndex::VisibleInstanceIndex, FrameResource.mVisibleInstanceIndexBuffer.Get(), FrameResource.mVisibleInstanceIndexSrvHandle.GetCPU(), SafeVisibleInstanceIndexCount, sizeof(std::uint32_t));
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

        auto CreateUavIfChanged = [this, &FrameResource, &UavDesc](EnvironmentGpuUavCacheIndex CacheIndex, ID3D12Resource* Resource, D3D12_CPU_DESCRIPTOR_HANDLE Handle, std::uint32_t ElementCount, std::uint32_t Stride) {
            EnvironmentGpuDescriptorCache& Cache{ FrameResource.mUavCaches[static_cast<std::size_t>(CacheIndex)] };
            if (IsEnvironmentGpuDescriptorCacheValid(Cache, Resource, ElementCount, Stride) == true) {
                return;
            }

            UavDesc.Buffer.NumElements = ElementCount;
            UavDesc.Buffer.StructureByteStride = Stride;
            mDevice->CreateUnorderedAccessView(Resource, nullptr, &UavDesc, Handle);
            Cache.mResource = Resource;
            Cache.mElementCount = ElementCount;
            Cache.mStride = Stride;
        };

        CreateUavIfChanged(EnvironmentGpuUavCacheIndex::InstanceContext, FrameResource.mInstanceContextBuffer.Get(), FrameResource.mInstanceContextUavHandle.GetCPU(), std::max(InstanceContextCount, 1u), sizeof(RenderContract::EnvironmentInstanceContext));
        CreateUavIfChanged(EnvironmentGpuUavCacheIndex::CandidateContext, FrameResource.mCandidateContextBuffer.Get(), FrameResource.mCandidateContextUavHandle.GetCPU(), std::max(CandidateContextCount, 1u), sizeof(EnvironmentGpuPlacementCandidate));
        CreateUavIfChanged(EnvironmentGpuUavCacheIndex::VisibleInstanceIndex, FrameResource.mVisibleInstanceIndexBuffer.Get(), FrameResource.mVisibleInstanceIndexUavHandle.GetCPU(), std::max(VisibleInstanceIndexCount, 1u), sizeof(std::uint32_t));
        CreateUavIfChanged(EnvironmentGpuUavCacheIndex::IndirectCommand, FrameResource.mIndirectArgumentBuffer.Get(), FrameResource.mIndirectArgumentUavHandle.GetCPU(), std::max(IndirectArgumentCount, 1u), sizeof(EnvironmentIndirectDrawCommand));
    }

    void EnvironmentRuntime::ResetGpuResources() {
        mComputeRootSignature.Reset();
        mIndirectCommandInitializePipelineState.Reset();
        mDenseCandidateGeneratePipelineState.Reset();
        mSpacedCandidateGeneratePipelineState.Reset();
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
        mGpuPersistentResource = EnvironmentGpuPersistentResource{};
        mGpuDrivenFrameResources = {};
        mGpuInstanceContextCount = 0u;
        mGpuStatusUavIndex = InvalidDescriptorIndex;
        mLastGpuDispatchFuture = RenderContract::Future{};
        mGpuDrivenEnabled = false;
        mGpuResourcesInitialized = false;
    }
}
