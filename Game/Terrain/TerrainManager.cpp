#include "Game/Terrain/TerrainManager.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <mutex>
#include <utility>

#include "Game/Model/TerrainHeightFieldFactory.h"
#include "Game/Model/TerrainSplatMapGenerator.h"
#include "Game/Model/TerrainTiledMeshBuilder.h"
#include "Utility/MathValidation.h"

#undef min
#undef max

namespace {
    constexpr float RaycastDistanceEpsilon{ 1.0E-4F };
    constexpr float RaycastDeltaEpsilon{ 1.0E-4F };

    std::uint32_t ResolveStreamingGridStep(const Game::TerrainBuildDesc& Desc) {
        if (Desc.mStreamingGridStep > 0U) {
            return Desc.mStreamingGridStep;
        }

        return std::max(Desc.TileQuadCount, 1U);
    }

    std::int32_t FloorToStep(std::int32_t Value, std::uint32_t Step) {
        const std::int32_t StepValue{ static_cast<std::int32_t>(std::max(Step, 1U)) };
        if (Value >= 0) {
            return (Value / StepValue) * StepValue;
        }

        return -(((-Value + StepValue - 1) / StepValue) * StepValue);
    }

    float CalculateStreamingWorldOrigin(std::int32_t OriginGrid, std::uint32_t HeightFieldVertexCount, float CellSize, bool CenterOrigin) {
        if (CenterOrigin == true) {
            const float HalfGrid{ HeightFieldVertexCount > 1U ? static_cast<float>(HeightFieldVertexCount - 1U) * 0.5F : 0.0F };
            return (static_cast<float>(OriginGrid) + HalfGrid) * CellSize;
        }

        return static_cast<float>(OriginGrid) * CellSize;
    }

    DirectX::SimpleMath::Matrix BuildTerrainWorldMatrix(const Game::TerrainWorldData& TerrainData) {
        DirectX::SimpleMath::Matrix ScalingMatrix{ DirectX::SimpleMath::Matrix::CreateScale(TerrainData.mScale) };
        DirectX::SimpleMath::Matrix RotationMatrix{ DirectX::SimpleMath::Matrix::CreateFromQuaternion(TerrainData.mOrientation) };
        DirectX::SimpleMath::Matrix TranslationMatrix{ DirectX::SimpleMath::Matrix::CreateTranslation(TerrainData.mPosition) };
        DirectX::SimpleMath::Matrix WorldMatrix{ ScalingMatrix * RotationMatrix * TranslationMatrix };
        return WorldMatrix;
    }

    bool IsTerrainWorldDataValid(const Game::TerrainWorldData& TerrainData) {
        if (TerrainData.mHeightFieldValues == nullptr) {
            return false;
        }

        const std::size_t ExpectedHeightValueCount{ static_cast<std::size_t>(TerrainData.mHeightFieldWidth) * static_cast<std::size_t>(TerrainData.mHeightFieldHeight) };
        return TerrainData.mHeightFieldWidth > 1U && TerrainData.mHeightFieldHeight > 1U && TerrainData.mHeightFieldCellSizeX > 0.0F && TerrainData.mHeightFieldCellSizeZ > 0.0F && TerrainData.mHeightFieldMaxHeight > 0.0F && TerrainData.mHeightFieldValues->size() == ExpectedHeightValueCount;
    }

    std::size_t CalculateHeightFieldIndex(const Game::TerrainWorldData& TerrainData, std::uint32_t X, std::uint32_t Z) {
        std::size_t Index{ static_cast<std::size_t>(Z) * static_cast<std::size_t>(TerrainData.mHeightFieldWidth) + static_cast<std::size_t>(X) };
        return Index;
    }

    float SampleCellHeight(const Game::TerrainWorldData& TerrainData, std::uint32_t X, std::uint32_t Z) {
        const std::size_t HeightFieldIndex{ CalculateHeightFieldIndex(TerrainData, X, Z) };
        const float Height01Value{ std::clamp((*TerrainData.mHeightFieldValues)[HeightFieldIndex], 0.0F, 1.0F) };
        return Height01Value * TerrainData.mHeightFieldMaxHeight;
    }

    bool TryResolveTerrainSurfaceAtLocalPosition(const Game::TerrainWorldData& TerrainData, float LocalX, float LocalZ, float& OutLocalHeight, DirectX::SimpleMath::Vector3& OutLocalNormal) {
        if (IsTerrainWorldDataValid(TerrainData) == false) {
            return false;
        }

        float GridPositionX{ LocalX };
        float GridPositionZ{ LocalZ };
        if (TerrainData.mHeightFieldCenterOrigin == true) {
            GridPositionX += (static_cast<float>(TerrainData.mHeightFieldWidth) - 1.0F) * TerrainData.mHeightFieldCellSizeX * 0.5F;
            GridPositionZ += (static_cast<float>(TerrainData.mHeightFieldHeight) - 1.0F) * TerrainData.mHeightFieldCellSizeZ * 0.5F;
        }

        const float MaxGridPositionX{ static_cast<float>(TerrainData.mHeightFieldWidth - 1U) * TerrainData.mHeightFieldCellSizeX };
        const float MaxGridPositionZ{ static_cast<float>(TerrainData.mHeightFieldHeight - 1U) * TerrainData.mHeightFieldCellSizeZ };
        if (GridPositionX < 0.0F || GridPositionZ < 0.0F || GridPositionX > MaxGridPositionX || GridPositionZ > MaxGridPositionZ) {
            return false;
        }

        const float GridX{ GridPositionX / TerrainData.mHeightFieldCellSizeX };
        const float GridZ{ GridPositionZ / TerrainData.mHeightFieldCellSizeZ };
        const std::uint32_t BaseGridX{ std::min(static_cast<std::uint32_t>(std::floor(GridX)), TerrainData.mHeightFieldWidth - 2U) };
        const std::uint32_t BaseGridZ{ std::min(static_cast<std::uint32_t>(std::floor(GridZ)), TerrainData.mHeightFieldHeight - 2U) };
        const std::uint32_t NextGridX{ BaseGridX + 1U };
        const std::uint32_t NextGridZ{ BaseGridZ + 1U };

        const float LocalGridX{ GridX - static_cast<float>(BaseGridX) };
        const float LocalGridZ{ GridZ - static_cast<float>(BaseGridZ) };
        const float Height00{ SampleCellHeight(TerrainData, BaseGridX, BaseGridZ) };
        const float Height10{ SampleCellHeight(TerrainData, NextGridX, BaseGridZ) };
        const float Height01{ SampleCellHeight(TerrainData, BaseGridX, NextGridZ) };
        const float Height11{ SampleCellHeight(TerrainData, NextGridX, NextGridZ) };
        const float HeightTop{ Height00 + ((Height10 - Height00) * LocalGridX) };
        const float HeightBottom{ Height01 + ((Height11 - Height01) * LocalGridX) };
        const float InterpolatedHeight{ HeightTop + ((HeightBottom - HeightTop) * LocalGridZ) };
        const float HeightDeltaX0{ Height10 - Height00 };
        const float HeightDeltaX1{ Height11 - Height01 };
        const float HeightDeltaZ0{ Height01 - Height00 };
        const float HeightDeltaZ1{ Height11 - Height10 };
        const float HeightDerivativeX{ ((1.0F - LocalGridZ) * HeightDeltaX0) + (LocalGridZ * HeightDeltaX1) };
        const float HeightDerivativeZ{ ((1.0F - LocalGridX) * HeightDeltaZ0) + (LocalGridX * HeightDeltaZ1) };
        const float SlopeX{ HeightDerivativeX / TerrainData.mHeightFieldCellSizeX };
        const float SlopeZ{ HeightDerivativeZ / TerrainData.mHeightFieldCellSizeZ };

        DirectX::SimpleMath::Vector3 SurfaceNormal{ -SlopeX, 1.0F, -SlopeZ };
        const float SurfaceNormalLengthSquared{ SurfaceNormal.LengthSquared() };
        if (SurfaceNormalLengthSquared <= 0.0F) {
            SurfaceNormal = DirectX::SimpleMath::Vector3::Up;
        }
        else {
            SurfaceNormal.Normalize();
        }

        OutLocalHeight = InterpolatedHeight;
        OutLocalNormal = SurfaceNormal;
        return true;
    }

    bool TryGetTerrainSurfaceAtWorldPosition(const Game::TerrainWorldData& TerrainData, float WorldX, float WorldZ, float& OutWorldHeight, DirectX::SimpleMath::Vector3& OutWorldNormal) {
        if (IsTerrainWorldDataValid(TerrainData) == false) {
            return false;
        }

        DirectX::SimpleMath::Matrix WorldMatrix{ BuildTerrainWorldMatrix(TerrainData) };
        DirectX::SimpleMath::Matrix InverseWorldMatrix{ WorldMatrix.Invert() };
        DirectX::SimpleMath::Matrix InverseTransposeWorldMatrix{ InverseWorldMatrix.Transpose() };
        DirectX::SimpleMath::Vector3 LocalPoint{ DirectX::SimpleMath::Vector3::Transform(DirectX::SimpleMath::Vector3{ WorldX, 0.0F, WorldZ }, InverseWorldMatrix) };

        float LocalHeight{};
        DirectX::SimpleMath::Vector3 LocalNormal{ DirectX::SimpleMath::Vector3::Up };
        const bool HasLocalSurface{ TryResolveTerrainSurfaceAtLocalPosition(TerrainData, LocalPoint.x, LocalPoint.z, LocalHeight, LocalNormal) };
        if (HasLocalSurface == false) {
            return false;
        }

        DirectX::SimpleMath::Vector3 WorldPoint{ DirectX::SimpleMath::Vector3::Transform(DirectX::SimpleMath::Vector3{ LocalPoint.x, LocalHeight, LocalPoint.z }, WorldMatrix) };
        DirectX::SimpleMath::Vector3 WorldNormal{ DirectX::SimpleMath::Vector3::TransformNormal(LocalNormal, InverseTransposeWorldMatrix) };
        const float WorldNormalLengthSquared{ WorldNormal.LengthSquared() };
        if (WorldNormalLengthSquared <= std::numeric_limits<float>::epsilon()) {
            WorldNormal = DirectX::SimpleMath::Vector3::Up;
        }
        else {
            WorldNormal /= std::sqrt(WorldNormalLengthSquared);
        }

        OutWorldHeight = WorldPoint.y;
        OutWorldNormal = WorldNormal;
        return true;
    }

    bool TryResolveTerrainRaySample(const Game::TerrainWorldData& TerrainData, const DirectX::SimpleMath::Ray& Ray, float RayDistance, float& OutTerrainDelta, DirectX::SimpleMath::Vector3& OutSurfacePosition, DirectX::SimpleMath::Vector3& OutSurfaceNormal) {
        const DirectX::SimpleMath::Vector3 RayPosition{ Ray.position + (Ray.direction * RayDistance) };
        if (MathUtility::IsFiniteVector3(RayPosition) == false) {
            return false;
        }

        float SurfaceHeight{};
        DirectX::SimpleMath::Vector3 SurfaceNormal{ DirectX::SimpleMath::Vector3::Up };
        if (TryGetTerrainSurfaceAtWorldPosition(TerrainData, RayPosition.x, RayPosition.z, SurfaceHeight, SurfaceNormal) == false || MathUtility::IsFiniteFloat(SurfaceHeight) == false || MathUtility::IsFiniteVector3(SurfaceNormal) == false) {
            return false;
        }

        const float TerrainDelta{ RayPosition.y - SurfaceHeight };
        if (MathUtility::IsFiniteFloat(TerrainDelta) == false) {
            return false;
        }

        OutTerrainDelta = TerrainDelta;
        OutSurfacePosition = DirectX::SimpleMath::Vector3{ RayPosition.x, SurfaceHeight, RayPosition.z };
        OutSurfaceNormal = SurfaceNormal;
        return true;
    }

    bool TryRaycastTerrainWorldData(const Game::TerrainWorldData& TerrainData, const DirectX::SimpleMath::Ray& Ray, float MaxDistance, DirectX::SimpleMath::Vector3& OutHitPosition, DirectX::SimpleMath::Vector3& OutHitNormal, float& OutHitDistance) {
        if (MathUtility::IsFiniteVector3(Ray.position) == false || MathUtility::IsFiniteVector3(Ray.direction) == false || MathUtility::IsFiniteFloat(MaxDistance) == false) {
            return false;
        }

        DirectX::SimpleMath::Vector3 SafeRayDirection{ Ray.direction };
        const float RayDirectionLengthSquared{ SafeRayDirection.LengthSquared() };
        if (MathUtility::IsFiniteFloat(RayDirectionLengthSquared) == false || RayDirectionLengthSquared <= RaycastDistanceEpsilon) {
            return false;
        }

        SafeRayDirection.Normalize();
        if (MathUtility::IsFiniteVector3(SafeRayDirection) == false) {
            return false;
        }

        const float SafeMaxDistance{ std::max(MaxDistance, 0.0F) };
        if (SafeMaxDistance <= RaycastDistanceEpsilon) {
            return false;
        }

        const DirectX::SimpleMath::Ray SafeRay{ Ray.position, SafeRayDirection };
        const float WorldCellSizeX{ std::abs(TerrainData.mHeightFieldCellSizeX * TerrainData.mScale.x) };
        const float WorldCellSizeZ{ std::abs(TerrainData.mHeightFieldCellSizeZ * TerrainData.mScale.z) };
        const float MinimumCellSize{ std::min(WorldCellSizeX, WorldCellSizeZ) };
        const float SampleStepDistance{ std::max(MinimumCellSize * 0.5F, 0.05F) };
        if (MathUtility::IsFiniteFloat(SampleStepDistance) == false || SampleStepDistance <= 0.0F) {
            return false;
        }

        const std::uint32_t SampleStepCount{ std::max(static_cast<std::uint32_t>(1U), static_cast<std::uint32_t>(std::ceil(SafeMaxDistance / SampleStepDistance))) };
        bool IsPreviousSampleResolved{};
        float PreviousSampleDistance{};
        float PreviousSampleDelta{};
        DirectX::SimpleMath::Vector3 PreviousSurfacePosition{};
        DirectX::SimpleMath::Vector3 PreviousSurfaceNormal{ DirectX::SimpleMath::Vector3::Up };

        if (TryResolveTerrainRaySample(TerrainData, SafeRay, 0.0F, PreviousSampleDelta, PreviousSurfacePosition, PreviousSurfaceNormal) == true) {
            if (PreviousSampleDelta <= RaycastDeltaEpsilon) {
                OutHitPosition = PreviousSurfacePosition;
                OutHitNormal = PreviousSurfaceNormal;
                OutHitDistance = 0.0F;
                return true;
            }

            IsPreviousSampleResolved = true;
        }

        for (std::uint32_t SampleIndex{ 1U }; SampleIndex <= SampleStepCount; ++SampleIndex) {
            const float CurrentSampleDistance{ std::min(SafeMaxDistance, static_cast<float>(SampleIndex) * SampleStepDistance) };
            float CurrentSampleDelta{};
            DirectX::SimpleMath::Vector3 CurrentSurfacePosition{};
            DirectX::SimpleMath::Vector3 CurrentSurfaceNormal{ DirectX::SimpleMath::Vector3::Up };
            const bool IsCurrentSampleResolved{ TryResolveTerrainRaySample(TerrainData, SafeRay, CurrentSampleDistance, CurrentSampleDelta, CurrentSurfacePosition, CurrentSurfaceNormal) };
            if (IsPreviousSampleResolved == true && IsCurrentSampleResolved == true) {
                const bool IsCrossedSurface{ (PreviousSampleDelta > 0.0F && CurrentSampleDelta <= 0.0F) || (PreviousSampleDelta < 0.0F && CurrentSampleDelta >= 0.0F) || std::abs(CurrentSampleDelta) <= RaycastDeltaEpsilon };
                if (IsCrossedSurface == true) {
                    float HitDistance{ CurrentSampleDistance };
                    const float DeltaDifference{ CurrentSampleDelta - PreviousSampleDelta };
                    if (std::abs(DeltaDifference) > RaycastDistanceEpsilon) {
                        const float HitAlpha{ std::clamp(PreviousSampleDelta / (PreviousSampleDelta - CurrentSampleDelta), 0.0F, 1.0F) };
                        HitDistance = PreviousSampleDistance + ((CurrentSampleDistance - PreviousSampleDistance) * HitAlpha);
                    }

                    float HitSampleDelta{};
                    DirectX::SimpleMath::Vector3 HitSurfacePosition{};
                    DirectX::SimpleMath::Vector3 HitSurfaceNormal{ DirectX::SimpleMath::Vector3::Up };
                    if (TryResolveTerrainRaySample(TerrainData, SafeRay, HitDistance, HitSampleDelta, HitSurfacePosition, HitSurfaceNormal) == false) {
                        HitDistance = CurrentSampleDistance;
                        HitSurfacePosition = CurrentSurfacePosition;
                        HitSurfaceNormal = CurrentSurfaceNormal;
                    }

                    OutHitPosition = HitSurfacePosition;
                    OutHitNormal = HitSurfaceNormal;
                    OutHitDistance = HitDistance;
                    return true;
                }
            }

            if (IsCurrentSampleResolved == true) {
                IsPreviousSampleResolved = true;
                PreviousSampleDistance = CurrentSampleDistance;
                PreviousSampleDelta = CurrentSampleDelta;
                PreviousSurfacePosition = CurrentSurfacePosition;
                PreviousSurfaceNormal = CurrentSurfaceNormal;
            }
        }

        return false;
    }
}

namespace Game {
    ITerrainQuery::ITerrainQuery() {
    }

    ITerrainQuery::~ITerrainQuery() {
    }

    ITerrainQuery::ITerrainQuery(const ITerrainQuery& Other) {
        (void)Other;
    }

    ITerrainQuery& ITerrainQuery::operator=(const ITerrainQuery& Other) {
        (void)Other;
        return *this;
    }

    ITerrainQuery::ITerrainQuery(ITerrainQuery&& Other) noexcept {
        (void)Other;
    }

    ITerrainQuery& ITerrainQuery::operator=(ITerrainQuery&& Other) noexcept {
        (void)Other;
        return *this;
    }

    ITerrainManager::ITerrainManager()
        : ITerrainQuery{} {
    }

    ITerrainManager::~ITerrainManager() {
    }

    ITerrainManager::ITerrainManager(const ITerrainManager& Other)
        : ITerrainQuery{ Other } {
    }

    ITerrainManager& ITerrainManager::operator=(const ITerrainManager& Other) {
        ITerrainQuery::operator=(Other);
        return *this;
    }

    ITerrainManager::ITerrainManager(ITerrainManager&& Other) noexcept
        : ITerrainQuery{ std::move(Other) } {
    }

    ITerrainManager& ITerrainManager::operator=(ITerrainManager&& Other) noexcept {
        ITerrainQuery::operator=(std::move(Other));
        return *this;
    }

    TerrainManager::TerrainManager()
        : ITerrainManager{},
          mTerrainDataMutex{},
          mTerrainDataItems{},
          mTerrainDataGenerations{} {
    }

    TerrainManager::~TerrainManager() {
    }

    void TerrainManager::Clear() {
        std::unique_lock<std::shared_mutex> TerrainDataLock{ mTerrainDataMutex };
        mTerrainDataItems.clear();
        mTerrainDataGenerations.clear();
    }

    TerrainDataHandle TerrainManager::UpsertTerrainData(std::uint32_t TerrainId, const DirectX::SimpleMath::Vector3& Position, const DirectX::SimpleMath::Vector3& Rotation, const DirectX::SimpleMath::Vector3& Scale, const TerrainBuildDesc& BuildDesc, const std::shared_ptr<const HeightFieldData>& HeightField) {
        if (HeightField == nullptr || HeightField->HeightValues.empty() == true) {
            return TerrainDataHandle{};
        }

        TerrainDataHandle Handle{};
        Handle.mValue = TerrainId;
        std::shared_ptr<const TerrainWorldData> TerrainData{};
        {
            std::unique_lock<std::shared_mutex> TerrainDataLock{ mTerrainDataMutex };
            if (Handle.mValue >= mTerrainDataItems.size()) {
                mTerrainDataItems.resize(static_cast<std::size_t>(Handle.mValue) + 1U);
                mTerrainDataGenerations.resize(static_cast<std::size_t>(Handle.mValue) + 1U);
            }

            mTerrainDataGenerations[Handle.mValue] += 1U;
            if (mTerrainDataGenerations[Handle.mValue] == 0U) {
                mTerrainDataGenerations[Handle.mValue] = 1U;
            }

            Handle.mGeneration = mTerrainDataGenerations[Handle.mValue];
            TerrainData = BuildTerrainWorldData(TerrainId, Position, Rotation, Scale, BuildDesc, HeightField, Handle);
            mTerrainDataItems[Handle.mValue] = TerrainData;
        }

        return Handle;
    }

    TerrainDataHandle TerrainManager::UpsertTerrainData(const TerrainWorldData& TerrainData) {
        if (IsTerrainWorldDataValid(TerrainData) == false) {
            return TerrainDataHandle{};
        }

        TerrainDataHandle Handle{};
        Handle.mValue = TerrainData.mTerrainId;
        {
            std::unique_lock<std::shared_mutex> TerrainDataLock{ mTerrainDataMutex };
            if (Handle.mValue >= mTerrainDataItems.size()) {
                mTerrainDataItems.resize(static_cast<std::size_t>(Handle.mValue) + 1U);
                mTerrainDataGenerations.resize(static_cast<std::size_t>(Handle.mValue) + 1U);
            }

            mTerrainDataGenerations[Handle.mValue] += 1U;
            if (mTerrainDataGenerations[Handle.mValue] == 0U) {
                mTerrainDataGenerations[Handle.mValue] = 1U;
            }

            Handle.mGeneration = mTerrainDataGenerations[Handle.mValue];
            TerrainWorldData StoredTerrainData{ TerrainData };
            StoredTerrainData.mHandle = Handle;
            StoredTerrainData.mTerrainId = Handle.mValue;
            mTerrainDataItems[Handle.mValue] = std::make_shared<const TerrainWorldData>(std::move(StoredTerrainData));
        }

        return Handle;
    }

    bool TerrainManager::RemoveTerrainData(TerrainDataHandle Handle) {
        if (IsTerrainDataHandleValid(Handle) == false) {
            return false;
        }

        std::unique_lock<std::shared_mutex> TerrainDataLock{ mTerrainDataMutex };
        if (Handle.mValue >= mTerrainDataItems.size() || mTerrainDataGenerations[Handle.mValue] != Handle.mGeneration || mTerrainDataItems[Handle.mValue] == nullptr) {
            return false;
        }

        mTerrainDataItems[Handle.mValue].reset();
        mTerrainDataGenerations[Handle.mValue] += 1U;
        return true;
    }

    TerrainStreamingBuildResult TerrainManager::BuildStreamingData(TerrainBuildDesc StreamingDesc, std::int32_t TargetOriginGridX, std::int32_t TargetOriginGridZ) const {
        TerrainStreamingBuildResult Result{};
        Result.mTargetOriginGridX = TargetOriginGridX;
        Result.mTargetOriginGridZ = TargetOriginGridZ;

        try {
            TerrainHeightFieldFactory HeightFieldFactory{};
            TerrainTiledMeshBuilder Builder{};
            TerrainSplatMapGenerator SplatMapGenerator{};
            StreamingDesc.mProceduralHeightFieldDesc = HeightFieldFactory.ResolveProceduralHeightFieldDesc(StreamingDesc);
            HeightFieldData HeightField{ HeightFieldFactory.Build(StreamingDesc) };
            SplatMapData SplatMap{ SplatMapGenerator.Generate(HeightField, StreamingDesc) };
            TerrainTiledMeshData TiledMeshData{ Builder.Build(HeightField, StreamingDesc) };
            Result.mHeightField = std::make_shared<const HeightFieldData>(std::move(HeightField));
            Result.mSplatMap = std::make_shared<const SplatMapData>(std::move(SplatMap));
            Result.mBuildDesc = std::move(StreamingDesc);
            Result.mTileMetadata = std::move(TiledMeshData.mTileMetadata);
            Result.mTileQuadCount = TiledMeshData.mTileQuadCount;
            Result.mTileCountX = TiledMeshData.mTileCountX;
            Result.mTileCountZ = TiledMeshData.mTileCountZ;
            Result.mLodCount = TiledMeshData.mLodCount;
            Result.mLodDistances = std::move(TiledMeshData.mLodDistances);
            Result.mLocalBoundingBox = TiledMeshData.mLocalBoundingBox;
            Result.mStreamWorldOriginX = CalculateStreamingWorldOrigin(TargetOriginGridX, Result.mHeightField->Width, Result.mBuildDesc.CellSizeX, Result.mBuildDesc.CenterOrigin);
            Result.mStreamWorldOriginZ = CalculateStreamingWorldOrigin(TargetOriginGridZ, Result.mHeightField->Height, Result.mBuildDesc.CellSizeZ, Result.mBuildDesc.CenterOrigin);
            Result.mSucceeded = true;
        }
        catch (const std::exception&) {
            Result.mSucceeded = false;
        }

        return Result;
    }

    bool TerrainManager::TryGetTerrainWorldData(TerrainDataHandle Handle, std::shared_ptr<const TerrainWorldData>& OutTerrainData) const {
        OutTerrainData.reset();
        if (IsTerrainDataHandleValid(Handle) == false) {
            return false;
        }

        std::shared_lock<std::shared_mutex> TerrainDataLock{ mTerrainDataMutex };
        if (Handle.mValue >= mTerrainDataItems.size() || mTerrainDataGenerations[Handle.mValue] != Handle.mGeneration || mTerrainDataItems[Handle.mValue] == nullptr) {
            return false;
        }

        OutTerrainData = mTerrainDataItems[Handle.mValue];
        return true;
    }

    bool TerrainManager::TryGetSurfaceAtWorldPosition(TerrainDataHandle Handle, float WorldX, float WorldZ, float& OutWorldHeight, DirectX::SimpleMath::Vector3& OutWorldNormal) const {
        std::shared_ptr<const TerrainWorldData> TerrainData{};
        if (TryGetTerrainWorldData(Handle, TerrainData) == false) {
            return false;
        }

        return TryGetTerrainSurfaceAtWorldPosition(*TerrainData, WorldX, WorldZ, OutWorldHeight, OutWorldNormal);
    }

    bool TerrainManager::TryGetSurfaceHeightAtWorldPosition(TerrainDataHandle Handle, float WorldX, float WorldZ, float& OutWorldHeight) const {
        DirectX::SimpleMath::Vector3 WorldNormal{ DirectX::SimpleMath::Vector3::Up };
        return TryGetSurfaceAtWorldPosition(Handle, WorldX, WorldZ, OutWorldHeight, WorldNormal);
    }

    bool TerrainManager::TryRaycast(TerrainDataHandle Handle, const DirectX::SimpleMath::Ray& Ray, float MaxDistance, DirectX::SimpleMath::Vector3& OutHitPosition, DirectX::SimpleMath::Vector3& OutHitNormal, float& OutHitDistance) const {
        std::shared_ptr<const TerrainWorldData> TerrainData{};
        if (TryGetTerrainWorldData(Handle, TerrainData) == false) {
            return false;
        }

        return TryRaycastTerrainWorldData(*TerrainData, Ray, MaxDistance, OutHitPosition, OutHitNormal, OutHitDistance);
    }

    bool TerrainManager::TryGetSurfaceAtWorldPosition(float WorldX, float WorldZ, float& OutWorldHeight, DirectX::SimpleMath::Vector3& OutWorldNormal) const {
        const std::vector<std::shared_ptr<const TerrainWorldData>> TerrainDataItems{ CollectTerrainDataSnapshot() };
        bool HasSurface{};
        float HighestSurfaceHeight{};
        DirectX::SimpleMath::Vector3 HighestSurfaceNormal{ DirectX::SimpleMath::Vector3::Up };
        for (const std::shared_ptr<const TerrainWorldData>& TerrainData : TerrainDataItems) {
            if (TerrainData == nullptr) {
                continue;
            }

            float SurfaceHeight{};
            DirectX::SimpleMath::Vector3 SurfaceNormal{ DirectX::SimpleMath::Vector3::Up };
            const bool HasCurrentSurface{ TryGetTerrainSurfaceAtWorldPosition(*TerrainData, WorldX, WorldZ, SurfaceHeight, SurfaceNormal) };
            if (HasCurrentSurface == false) {
                continue;
            }

            if (HasSurface == false || SurfaceHeight > HighestSurfaceHeight) {
                HighestSurfaceHeight = SurfaceHeight;
                HighestSurfaceNormal = SurfaceNormal;
                HasSurface = true;
            }
        }

        if (HasSurface == false) {
            return false;
        }

        OutWorldHeight = HighestSurfaceHeight;
        OutWorldNormal = HighestSurfaceNormal;
        return true;
    }

    bool TerrainManager::TryGetSurfaceHeightAtWorldPosition(float WorldX, float WorldZ, float& OutWorldHeight) const {
        DirectX::SimpleMath::Vector3 WorldNormal{ DirectX::SimpleMath::Vector3::Up };
        return TryGetSurfaceAtWorldPosition(WorldX, WorldZ, OutWorldHeight, WorldNormal);
    }

    bool TerrainManager::TryRaycast(const DirectX::SimpleMath::Ray& Ray, float MaxDistance, DirectX::SimpleMath::Vector3& OutHitPosition, DirectX::SimpleMath::Vector3& OutHitNormal, float& OutHitDistance) const {
        const std::vector<std::shared_ptr<const TerrainWorldData>> TerrainDataItems{ CollectTerrainDataSnapshot() };
        bool HasHit{};
        float NearestHitDistance{ MaxDistance };
        DirectX::SimpleMath::Vector3 NearestHitPosition{};
        DirectX::SimpleMath::Vector3 NearestHitNormal{ DirectX::SimpleMath::Vector3::Up };
        for (const std::shared_ptr<const TerrainWorldData>& TerrainData : TerrainDataItems) {
            if (TerrainData == nullptr) {
                continue;
            }

            DirectX::SimpleMath::Vector3 HitPosition{};
            DirectX::SimpleMath::Vector3 HitNormal{ DirectX::SimpleMath::Vector3::Up };
            float HitDistance{};
            const bool HasCurrentHit{ TryRaycastTerrainWorldData(*TerrainData, Ray, MaxDistance, HitPosition, HitNormal, HitDistance) };
            if (HasCurrentHit == false || HitDistance < 0.0F || HitDistance > MaxDistance) {
                continue;
            }

            if (HasHit == false || HitDistance < NearestHitDistance) {
                HasHit = true;
                NearestHitDistance = HitDistance;
                NearestHitPosition = HitPosition;
                NearestHitNormal = HitNormal;
            }
        }

        if (HasHit == false) {
            return false;
        }

        OutHitPosition = NearestHitPosition;
        OutHitNormal = NearestHitNormal;
        OutHitDistance = NearestHitDistance;
        return true;
    }

    std::shared_ptr<const TerrainWorldData> TerrainManager::BuildTerrainWorldData(std::uint32_t TerrainId, const DirectX::SimpleMath::Vector3& Position, const DirectX::SimpleMath::Vector3& Rotation, const DirectX::SimpleMath::Vector3& Scale, const TerrainBuildDesc& BuildDesc, const std::shared_ptr<const HeightFieldData>& HeightField, TerrainDataHandle Handle) const {
        TerrainWorldData TerrainData{};
        TerrainData.mHandle = Handle;
        TerrainData.mTerrainId = TerrainId;
        TerrainData.mPosition = Position;
        TerrainData.mRotation = Rotation;
        TerrainData.mOrientation = DirectX::SimpleMath::Quaternion::CreateFromYawPitchRoll(Rotation.y, Rotation.x, Rotation.z);
        if (TerrainData.mOrientation.LengthSquared() <= 0.0F) {
            TerrainData.mOrientation = DirectX::SimpleMath::Quaternion{ 0.0F, 0.0F, 0.0F, 1.0F };
        }
        else {
            TerrainData.mOrientation.Normalize();
        }

        TerrainData.mScale = Scale;
        TerrainData.mHeightFieldWidth = HeightField->Width;
        TerrainData.mHeightFieldHeight = HeightField->Height;
        TerrainData.mHeightFieldCellSizeX = BuildDesc.CellSizeX;
        TerrainData.mHeightFieldCellSizeZ = BuildDesc.CellSizeZ;
        TerrainData.mHeightFieldMaxHeight = BuildDesc.MaxHeight;
        TerrainData.mHeightFieldCenterOrigin = BuildDesc.CenterOrigin;
        TerrainData.mHalfExtentX = TerrainData.mHeightFieldWidth > 1U ? static_cast<float>(TerrainData.mHeightFieldWidth - 1U) * BuildDesc.CellSizeX * 0.5F : 0.0F;
        TerrainData.mHalfExtentZ = TerrainData.mHeightFieldHeight > 1U ? static_cast<float>(TerrainData.mHeightFieldHeight - 1U) * BuildDesc.CellSizeZ * 0.5F : 0.0F;
        TerrainData.mHeightFieldValues = std::shared_ptr<const std::vector<float>>{ HeightField, &HeightField->HeightValues };
        return std::make_shared<const TerrainWorldData>(std::move(TerrainData));
    }

    std::vector<std::shared_ptr<const TerrainWorldData>> TerrainManager::CollectTerrainDataSnapshot() const {
        std::shared_lock<std::shared_mutex> TerrainDataLock{ mTerrainDataMutex };
        std::vector<std::shared_ptr<const TerrainWorldData>> TerrainDataItems{};
        TerrainDataItems.reserve(mTerrainDataItems.size());
        for (const std::shared_ptr<const TerrainWorldData>& TerrainData : mTerrainDataItems) {
            if (TerrainData != nullptr) {
                TerrainDataItems.push_back(TerrainData);
            }
        }

        return TerrainDataItems;
    }

    bool IsTerrainDataHandleValid(TerrainDataHandle Handle) {
        return Handle.mValue != std::numeric_limits<std::uint32_t>::max() && Handle.mGeneration != 0U;
    }
}
