#include "SceneYamlInternal.h"

namespace Game::SceneYaml {
    SimpleMath::Matrix BuildTransformOnlyWorldMatrix(const Game::Transform& TransformComponent) {
        const SimpleMath::Matrix TransformOnlyWorldMatrix{ SimpleMath::Matrix::CreateScale(TransformComponent.scale) * SimpleMath::Matrix::CreateFromQuaternion(TransformComponent.rotation) * SimpleMath::Matrix::CreateTranslation(TransformComponent.position) };
        return TransformOnlyWorldMatrix;
    }

    SimpleMath::Matrix BuildTransformOffsetMatrix(const Game::Transform& TransformComponent) {
        const SimpleMath::Matrix TransformOffsetMatrix{ SimpleMath::Matrix::CreateScale(TransformComponent.scale) * SimpleMath::Matrix::CreateFromQuaternion(TransformComponent.rotation) };
        return TransformOffsetMatrix;
    }

    std::vector<SimpleMath::Vector2> BuildTerrainSnapSamplePoints(const Game::BoundingBox* BoundingBoxComponent, const Game::Transform& TransformComponent) {
        std::vector<SimpleMath::Vector2> SamplePoints{};
        SamplePoints.reserve(5u);
        SamplePoints.push_back(SimpleMath::Vector2{ TransformComponent.position.x, TransformComponent.position.z });

        if (BoundingBoxComponent == nullptr) {
            return SamplePoints;
        }

        DirectX::XMFLOAT3 Corners[8]{};
        BoundingBoxComponent->GetObb().GetCorners(Corners);

        const SimpleMath::Matrix TransformOffsetMatrix{ BuildTransformOffsetMatrix(TransformComponent) };
        std::array<SimpleMath::Vector3, 8> OffsetPositions{};
        float BottomOffsetY{ std::numeric_limits<float>::max() };
        for (std::size_t CornerIndex{ 0u }; CornerIndex < OffsetPositions.size(); ++CornerIndex) {
            const DirectX::XMFLOAT3& Corner{ Corners[CornerIndex] };
            OffsetPositions[CornerIndex] = SimpleMath::Vector3::Transform(SimpleMath::Vector3{ Corner.x, Corner.y, Corner.z }, TransformOffsetMatrix);
            BottomOffsetY = std::min(BottomOffsetY, OffsetPositions[CornerIndex].y);
        }

        if (BottomOffsetY == std::numeric_limits<float>::max()) {
            return SamplePoints;
        }

        constexpr float BottomSampleEpsilon{ 0.001f };
        for (const SimpleMath::Vector3& OffsetPosition : OffsetPositions) {
            if (OffsetPosition.y > BottomOffsetY + BottomSampleEpsilon) {
                continue;
            }

            SamplePoints.push_back(SimpleMath::Vector2{ TransformComponent.position.x + OffsetPosition.x, TransformComponent.position.z + OffsetPosition.z });
        }

        return SamplePoints;
    }

    float CalculateBottomOffsetY(const Game::BoundingBox* BoundingBoxComponent, const Game::Transform& TransformComponent) {
        if (BoundingBoxComponent == nullptr) {
            return 0.0f;
        }

        DirectX::XMFLOAT3 Corners[8]{};
        BoundingBoxComponent->GetObb().GetCorners(Corners);

        const SimpleMath::Matrix TransformOffsetMatrix{ BuildTransformOffsetMatrix(TransformComponent) };
        float BottomOffsetY{ std::numeric_limits<float>::max() };
        for (const DirectX::XMFLOAT3& Corner : Corners) {
            const SimpleMath::Vector3 OffsetPosition{ SimpleMath::Vector3::Transform(SimpleMath::Vector3{ Corner.x, Corner.y, Corner.z }, TransformOffsetMatrix) };
            if (OffsetPosition.y < BottomOffsetY) {
                BottomOffsetY = OffsetPosition.y;
            }
        }

        if (BottomOffsetY == std::numeric_limits<float>::max()) {
            return 0.0f;
        }

        return BottomOffsetY;
    }

    bool TryResolveHighestTerrainSurfaceHeight(const Arche::World& World, const std::vector<TerrainSurfaceBinding>& TerrainSurfaceBindings, float WorldX, float WorldZ, float& OutSurfaceHeight) {
        bool HasSurfaceHeight{};
        float HighestSurfaceHeight{};

        for (const TerrainSurfaceBinding& TerrainSurfaceBindingItem : TerrainSurfaceBindings) {
            if (TerrainSurfaceBindingItem.mEntityId == Arche::NullEntityID) {
                continue;
            }

            const Game::Transform* TerrainTransformComponent{ World.GetComponent<Game::Transform>(TerrainSurfaceBindingItem.mEntityId) };
            if (TerrainTransformComponent == nullptr) {
                continue;
            }

            PhysicsTerrainActor::ActorDesc TerrainActorDesc{ TerrainSurfaceBindingItem.mTerrainActorDesc };
            TerrainActorDesc.Position = TerrainTransformComponent->position;
            TerrainActorDesc.Rotation = TerrainTransformComponent->rotationEuler;
            TerrainActorDesc.Scale = TerrainTransformComponent->scale;

            PhysicsTerrainActor TerrainActor{ TerrainActorDesc };
            TerrainActor.SetOrientation(TerrainTransformComponent->rotation);

            float SurfaceHeight{};
            const bool HasCurrentSurfaceHeight{ TerrainActor.TryGetSurfaceHeightAtWorldPosition(WorldX, WorldZ, SurfaceHeight) };
            if (HasCurrentSurfaceHeight == false) {
                continue;
            }

            if (HasSurfaceHeight == false || SurfaceHeight > HighestSurfaceHeight) {
                HighestSurfaceHeight = SurfaceHeight;
                HasSurfaceHeight = true;
            }
        }

        if (HasSurfaceHeight == false) {
            return false;
        }

        OutSurfaceHeight = HighestSurfaceHeight;
        return true;
    }

    bool TryResolveHighestTerrainSurfaceHeight(const Arche::World& World, const std::vector<TerrainSurfaceBinding>& TerrainSurfaceBindings, const std::vector<SimpleMath::Vector2>& WorldSamplePoints, float& OutSurfaceHeight) {
        bool HasSurfaceHeight{};
        float HighestSurfaceHeight{};

        for (const SimpleMath::Vector2& WorldSamplePoint : WorldSamplePoints) {
            float SurfaceHeight{};
            const bool HasCurrentSurfaceHeight{ TryResolveHighestTerrainSurfaceHeight(World, TerrainSurfaceBindings, WorldSamplePoint.x, WorldSamplePoint.y, SurfaceHeight) };
            if (HasCurrentSurfaceHeight == false) {
                continue;
            }

            if (HasSurfaceHeight == false || SurfaceHeight > HighestSurfaceHeight) {
                HighestSurfaceHeight = SurfaceHeight;
                HasSurfaceHeight = true;
            }
        }

        if (HasSurfaceHeight == false) {
            return false;
        }

        OutSurfaceHeight = HighestSurfaceHeight;
        return true;
    }

    bool TryBuildTerrainActorDescFromHeightField(const Game::HeightFieldData& HeightFieldDataValue, const Game::TerrainBuildDesc& TerrainBuildDescValue, PhysicsTerrainActor::ActorDesc& OutTerrainActorDesc) {
        if (HeightFieldDataValue.Width == 0u || HeightFieldDataValue.Height == 0u || HeightFieldDataValue.HeightValues.empty() == true) {
            return false;
        }

        OutTerrainActorDesc = PhysicsTerrainActor::BuildHeightFieldActorDesc(HeightFieldDataValue.Width, HeightFieldDataValue.Height, HeightFieldDataValue.HeightValues, TerrainBuildDescValue.MaxHeight, TerrainBuildDescValue.CellSizeX, TerrainBuildDescValue.CellSizeZ, TerrainBuildDescValue.CenterOrigin);
        return true;
    }

    bool TryBuildTerrainActorDescFromRenderResource(const Game::TerrainRenderResource& TerrainResource, PhysicsTerrainActor::ActorDesc& OutTerrainActorDesc) {
        const Game::TerrainBuildDesc& TerrainBuildDescValue{ TerrainResource.GetBuildDesc() };
        const Game::HeightFieldData& ResourceHeightFieldData{ TerrainResource.GetHeightFieldData() };
        const bool IsResourceHeightFieldResolved{ TryBuildTerrainActorDescFromHeightField(ResourceHeightFieldData, TerrainBuildDescValue, OutTerrainActorDesc) };
        if (IsResourceHeightFieldResolved == true) {
            return true;
        }

        try {
            Game::TerrainHeightFieldFactory HeightFieldFactory{};
            const Game::HeightFieldData BuiltHeightFieldData{ HeightFieldFactory.Build(TerrainBuildDescValue) };
            return TryBuildTerrainActorDescFromHeightField(BuiltHeightFieldData, TerrainBuildDescValue, OutTerrainActorDesc);
        }
        catch (const std::exception&) {
            return false;
        }
    }

    std::uint32_t ResolveTerrainStreamingGridStep(const Game::TerrainBuildDesc& TerrainBuildDescValue) {
        if (TerrainBuildDescValue.mStreamingGridStep > 0u) {
            return TerrainBuildDescValue.mStreamingGridStep;
        }

        return std::max(TerrainBuildDescValue.TileQuadCount, 1u);
    }

    std::int32_t FloorTerrainStreamingGridToStep(std::int32_t Value, std::uint32_t Step) {
        const std::int32_t StepValue{ static_cast<std::int32_t>(std::max(Step, 1u)) };
        if (Value >= 0) {
            return (Value / StepValue) * StepValue;
        }

        return -(((-Value + StepValue - 1) / StepValue) * StepValue);
    }

    std::int32_t CalculateTerrainStreamingOriginGrid(float FocusPosition, float CellSize, std::uint32_t HeightFieldVertexCount, std::uint32_t Step) {
        const float SafeCellSize{ CellSize > 0.0f ? CellSize : 1.0f };
        const std::uint32_t QuadCount{ HeightFieldVertexCount > 1u ? HeightFieldVertexCount - 1u : 1u };
        const std::int32_t FocusGrid{ static_cast<std::int32_t>(std::floor(FocusPosition / SafeCellSize)) };
        const std::int32_t HalfGrid{ static_cast<std::int32_t>(QuadCount / 2u) };
        return FloorTerrainStreamingGridToStep(FocusGrid - HalfGrid, Step);
    }

    float CalculateTerrainStreamingWorldOrigin(std::int32_t OriginGrid, std::uint32_t HeightFieldVertexCount, float CellSize, bool CenterOrigin) {
        if (CenterOrigin == true) {
            const float HalfGrid{ HeightFieldVertexCount > 1u ? static_cast<float>(HeightFieldVertexCount - 1u) * 0.5f : 0.0f };
            return (static_cast<float>(OriginGrid) + HalfGrid) * CellSize;
        }

        return static_cast<float>(OriginGrid) * CellSize;
    }

    bool TryResolveTerrainStreamingFocusPosition(Arche::World& World, SimpleMath::Vector3& OutFocusPosition) {
        for (auto [TransformComponent, CameraComponent] : World.Query<Game::Transform, Game::Camera>()) {
            if (CameraComponent.isActive == false) {
                continue;
            }

            OutFocusPosition = TransformComponent.position;
            return true;
        }

        return false;
    }

    bool TryPrepareInitialStreamingTerrainBuildDesc(Arche::World& World, Game::TerrainBuildDesc& InOutTerrainBuildDesc) {
        if (InOutTerrainBuildDesc.mStreamingEnabled == false || InOutTerrainBuildDesc.mHeightSourceType != Game::TerrainHeightSourceType::Procedural) {
            return true;
        }

        try {
            Game::TerrainHeightFieldFactory HeightFieldFactory{};
            InOutTerrainBuildDesc.mProceduralHeightFieldDesc = HeightFieldFactory.ResolveProceduralHeightFieldDesc(InOutTerrainBuildDesc);
        }
        catch (const std::exception&) {
            return false;
        }

        SimpleMath::Vector3 FocusPosition{};
        const bool HasFocusPosition{ TryResolveTerrainStreamingFocusPosition(World, FocusPosition) };
        if (HasFocusPosition == false) {
            return true;
        }

        const std::uint32_t StreamingGridStep{ ResolveTerrainStreamingGridStep(InOutTerrainBuildDesc) };
        InOutTerrainBuildDesc.mProceduralHeightFieldDesc.mSampleOffsetX = CalculateTerrainStreamingOriginGrid(FocusPosition.x, InOutTerrainBuildDesc.CellSizeX, InOutTerrainBuildDesc.mProceduralHeightFieldDesc.mWidth, StreamingGridStep);
        InOutTerrainBuildDesc.mProceduralHeightFieldDesc.mSampleOffsetZ = CalculateTerrainStreamingOriginGrid(FocusPosition.z, InOutTerrainBuildDesc.CellSizeZ, InOutTerrainBuildDesc.mProceduralHeightFieldDesc.mHeight, StreamingGridStep);
        return true;
    }

    void ApplyInitialStreamingTerrainTransform(const Game::TerrainRenderResource& TerrainResource, Game::Transform* TransformComponent) {
        if (TransformComponent == nullptr) {
            return;
        }

        const Game::TerrainBuildDesc& TerrainBuildDescValue{ TerrainResource.GetBuildDesc() };
        if (TerrainBuildDescValue.mStreamingEnabled == false || TerrainBuildDescValue.mHeightSourceType != Game::TerrainHeightSourceType::Procedural) {
            return;
        }

        const std::uint32_t HeightFieldWidth{ TerrainResource.GetHeightFieldWidth() > 1u ? TerrainResource.GetHeightFieldWidth() : TerrainBuildDescValue.mProceduralHeightFieldDesc.mWidth };
        const std::uint32_t HeightFieldHeight{ TerrainResource.GetHeightFieldHeight() > 1u ? TerrainResource.GetHeightFieldHeight() : TerrainBuildDescValue.mProceduralHeightFieldDesc.mHeight };
        TransformComponent->position.x = CalculateTerrainStreamingWorldOrigin(TerrainBuildDescValue.mProceduralHeightFieldDesc.mSampleOffsetX, HeightFieldWidth, TerrainBuildDescValue.CellSizeX, TerrainBuildDescValue.CenterOrigin);
        TransformComponent->position.z = CalculateTerrainStreamingWorldOrigin(TerrainBuildDescValue.mProceduralHeightFieldDesc.mSampleOffsetZ, HeightFieldHeight, TerrainBuildDescValue.CellSizeZ, TerrainBuildDescValue.CenterOrigin);
    }

    void ApplyPendingTerrainSnapBindings(Arche::World& World, const std::vector<TerrainSurfaceBinding>& TerrainSurfaceBindings, const std::vector<PendingTerrainSnapBinding>& PendingTerrainSnapBindings) {
        for (const PendingTerrainSnapBinding& PendingTerrainSnapBindingItem : PendingTerrainSnapBindings) {
            if (PendingTerrainSnapBindingItem.mEntityId == Arche::NullEntityID) {
                continue;
            }

            Game::Transform* TransformComponent{ World.GetComponent<Game::Transform>(PendingTerrainSnapBindingItem.mEntityId) };
            if (TransformComponent == nullptr) {
                continue;
            }

            Game::BoundingBox* BoundingBoxComponent{ World.GetComponent<Game::BoundingBox>(PendingTerrainSnapBindingItem.mEntityId) };
            const std::vector<SimpleMath::Vector2> SamplePoints{ BuildTerrainSnapSamplePoints(BoundingBoxComponent, *TransformComponent) };
            float SurfaceHeight{};
            const bool HasSurfaceHeight{ TryResolveHighestTerrainSurfaceHeight(World, TerrainSurfaceBindings, SamplePoints, SurfaceHeight) };
            if (HasSurfaceHeight == false) {
                continue;
            }

            const float BottomOffsetY{ CalculateBottomOffsetY(BoundingBoxComponent, *TransformComponent) };
            TransformComponent->position.y = SurfaceHeight - BottomOffsetY + PendingTerrainSnapBindingItem.mOffsetY;

            if (BoundingBoxComponent != nullptr) {
                const SimpleMath::Matrix TransformOnlyWorldMatrix{ BuildTransformOnlyWorldMatrix(*TransformComponent) };
                BoundingBoxComponent->UpdateWorldObb(TransformOnlyWorldMatrix);
            }
        }
    }

    bool TryReadTerrainSplatMapExpressionEntry(c4::yml::ConstNodeRef EntryNode, std::string& OutName, std::string& OutFormula) {
        if (EntryNode.readable() == false || EntryNode.is_map() == false) {
            return false;
        }

        TryReadStringChild(EntryNode, { "Name" }, OutName);
        TryReadStringChild(EntryNode, { "Formula" }, OutFormula);
        return OutName.empty() == false && OutFormula.empty() == false;
    }

    bool TryReadTerrainSplatMapDesc(c4::yml::ConstNodeRef SplatMapNode, Game::TerrainProceduralHeightFieldDesc::TerrainSplatMapDesc& OutDesc) {
        if (SplatMapNode.readable() == false || SplatMapNode.is_map() == false) {
            return false;
        }

        if (SplatMapNode.has_child("FallbackLayerIndex")) {
            SplatMapNode["FallbackLayerIndex"] >> OutDesc.mFallbackLayerIndex;
        }

        TryReadBoolChild(SplatMapNode, { "NormalizeWeights" }, OutDesc.mNormalizeWeights);

        if (SplatMapNode.has_child("MinimumWeightSum")) {
            SplatMapNode["MinimumWeightSum"] >> OutDesc.mMinimumWeightSum;
        }

        if (SplatMapNode.has_child("Variables")) {
            const c4::yml::ConstNodeRef VariablesNode{ SplatMapNode["Variables"] };
            if (VariablesNode.is_seq() == false) {
                return false;
            }

            OutDesc.mVariables.clear();
            for (const c4::yml::ConstNodeRef VariableNode : VariablesNode.children()) {
                Game::TerrainProceduralHeightFieldDesc::TerrainSplatMapVariableDesc VariableDesc{};
                if (TryReadTerrainSplatMapExpressionEntry(VariableNode, VariableDesc.mName, VariableDesc.mFormula) == false) {
                    return false;
                }

                OutDesc.mVariables.push_back(std::move(VariableDesc));
            }
        }

        if (SplatMapNode.has_child("Layers")) {
            const c4::yml::ConstNodeRef LayersNode{ SplatMapNode["Layers"] };
            if (LayersNode.is_seq() == false) {
                return false;
            }

            OutDesc.mLayers.clear();
            for (const c4::yml::ConstNodeRef LayerNode : LayersNode.children()) {
                Game::TerrainProceduralHeightFieldDesc::TerrainSplatMapLayerDesc LayerDesc{};
                if (TryReadTerrainSplatMapExpressionEntry(LayerNode, LayerDesc.mName, LayerDesc.mFormula) == false) {
                    return false;
                }

                OutDesc.mLayers.push_back(std::move(LayerDesc));
            }
        }

        return true;
    }

    bool TryReadTerrainProceduralHeightFieldDesc(c4::yml::ConstNodeRef ProceduralNode, Game::TerrainProceduralHeightFieldDesc& OutDesc) {
        if (ProceduralNode.readable() == false || ProceduralNode.is_map() == false) {
            return false;
        }

        if (ProceduralNode.has_child("Width")) {
            ProceduralNode["Width"] >> OutDesc.mWidth;
        }

        if (ProceduralNode.has_child("Height")) {
            ProceduralNode["Height"] >> OutDesc.mHeight;
        }

        if (ProceduralNode.has_child("Seed")) {
            ProceduralNode["Seed"] >> OutDesc.mSeed;
        }

        TryReadBoolChild(ProceduralNode, { "UseRandomSeed" }, OutDesc.mUseRandomSeed);

        if (ProceduralNode.has_child("OctaveCount")) {
            ProceduralNode["OctaveCount"] >> OutDesc.mOctaveCount;
        }

        if (ProceduralNode.has_child("NoiseScale")) {
            ProceduralNode["NoiseScale"] >> OutDesc.mNoiseScale;
        }

        if (ProceduralNode.has_child("Persistence")) {
            ProceduralNode["Persistence"] >> OutDesc.mPersistence;
        }

        if (ProceduralNode.has_child("Lacunarity")) {
            ProceduralNode["Lacunarity"] >> OutDesc.mLacunarity;
        }

        if (ProceduralNode.has_child("BaseHeight")) {
            ProceduralNode["BaseHeight"] >> OutDesc.mBaseHeight;
        }

        if (ProceduralNode.has_child("HeightAmplitude")) {
            ProceduralNode["HeightAmplitude"] >> OutDesc.mHeightAmplitude;
        }

        if (ProceduralNode.has_child("LodExponent")) {
            ProceduralNode["LodExponent"] >> OutDesc.mLodExponent;
        }

        if (ProceduralNode.has_child("SmoothingPassCount")) {
            ProceduralNode["SmoothingPassCount"] >> OutDesc.mSmoothingPassCount;
        }

        if (ProceduralNode.has_child("MinimumWidth")) {
            ProceduralNode["MinimumWidth"] >> OutDesc.mMinimumWidth;
        }

        if (ProceduralNode.has_child("MinimumHeight")) {
            ProceduralNode["MinimumHeight"] >> OutDesc.mMinimumHeight;
        }

        if (ProceduralNode.has_child("MaximumOctaveCount")) {
            ProceduralNode["MaximumOctaveCount"] >> OutDesc.mMaximumOctaveCount;
        }

        if (ProceduralNode.has_child("MaximumSmoothingPassCount")) {
            ProceduralNode["MaximumSmoothingPassCount"] >> OutDesc.mMaximumSmoothingPassCount;
        }

        if (ProceduralNode.has_child("MinimumHeightValue")) {
            ProceduralNode["MinimumHeightValue"] >> OutDesc.mMinimumHeightValue;
        }

        if (ProceduralNode.has_child("MaximumHeightValue")) {
            ProceduralNode["MaximumHeightValue"] >> OutDesc.mMaximumHeightValue;
        }

        if (ProceduralNode.has_child("SampleScaleX")) {
            ProceduralNode["SampleScaleX"] >> OutDesc.mSampleScaleX;
        }

        if (ProceduralNode.has_child("SampleScaleZ")) {
            ProceduralNode["SampleScaleZ"] >> OutDesc.mSampleScaleZ;
        }

        if (ProceduralNode.has_child("SampleOffsetX")) {
            ProceduralNode["SampleOffsetX"] >> OutDesc.mSampleOffsetX;
        }

        if (ProceduralNode.has_child("SampleOffsetZ")) {
            ProceduralNode["SampleOffsetZ"] >> OutDesc.mSampleOffsetZ;
        }

        if (ProceduralNode.has_child("InitialFrequency")) {
            ProceduralNode["InitialFrequency"] >> OutDesc.mInitialFrequency;
        }

        if (ProceduralNode.has_child("InitialAmplitude")) {
            ProceduralNode["InitialAmplitude"] >> OutDesc.mInitialAmplitude;
        }

        if (ProceduralNode.has_child("OctaveSeedStep")) {
            ProceduralNode["OctaveSeedStep"] >> OutDesc.mOctaveSeedStep;
        }

        if (ProceduralNode.has_child("NoiseNormalizationScale")) {
            ProceduralNode["NoiseNormalizationScale"] >> OutDesc.mNoiseNormalizationScale;
        }

        if (ProceduralNode.has_child("NoiseNormalizationBias")) {
            ProceduralNode["NoiseNormalizationBias"] >> OutDesc.mNoiseNormalizationBias;
        }

        if (ProceduralNode.has_child("HashShiftA")) {
            ProceduralNode["HashShiftA"] >> OutDesc.mHashShiftA;
        }

        if (ProceduralNode.has_child("HashShiftB")) {
            ProceduralNode["HashShiftB"] >> OutDesc.mHashShiftB;
        }

        if (ProceduralNode.has_child("HashShiftC")) {
            ProceduralNode["HashShiftC"] >> OutDesc.mHashShiftC;
        }

        if (ProceduralNode.has_child("HashShiftLimitExclusive")) {
            ProceduralNode["HashShiftLimitExclusive"] >> OutDesc.mHashShiftLimitExclusive;
        }

        if (ProceduralNode.has_child("HashMultiplierA")) {
            ProceduralNode["HashMultiplierA"] >> OutDesc.mHashMultiplierA;
        }

        if (ProceduralNode.has_child("HashMultiplierB")) {
            ProceduralNode["HashMultiplierB"] >> OutDesc.mHashMultiplierB;
        }

        if (ProceduralNode.has_child("HashCoordinateOffsetX")) {
            ProceduralNode["HashCoordinateOffsetX"] >> OutDesc.mHashCoordinateOffsetX;
        }

        if (ProceduralNode.has_child("HashCoordinateOffsetZ")) {
            ProceduralNode["HashCoordinateOffsetZ"] >> OutDesc.mHashCoordinateOffsetZ;
        }

        if (ProceduralNode.has_child("GradientDirectionCount")) {
            ProceduralNode["GradientDirectionCount"] >> OutDesc.mGradientDirectionCount;
        }

        if (ProceduralNode.has_child("FadeCoefficientA")) {
            ProceduralNode["FadeCoefficientA"] >> OutDesc.mFadeCoefficientA;
        }

        if (ProceduralNode.has_child("FadeCoefficientB")) {
            ProceduralNode["FadeCoefficientB"] >> OutDesc.mFadeCoefficientB;
        }

        if (ProceduralNode.has_child("FadeCoefficientC")) {
            ProceduralNode["FadeCoefficientC"] >> OutDesc.mFadeCoefficientC;
        }

        if (ProceduralNode.has_child("SmoothingCornerWeight")) {
            ProceduralNode["SmoothingCornerWeight"] >> OutDesc.mSmoothingCornerWeight;
        }

        if (ProceduralNode.has_child("SmoothingEdgeWeight")) {
            ProceduralNode["SmoothingEdgeWeight"] >> OutDesc.mSmoothingEdgeWeight;
        }

        if (ProceduralNode.has_child("SmoothingCenterWeight")) {
            ProceduralNode["SmoothingCenterWeight"] >> OutDesc.mSmoothingCenterWeight;
        }

        if (ProceduralNode.has_child("SmoothingWeightSum")) {
            ProceduralNode["SmoothingWeightSum"] >> OutDesc.mSmoothingWeightSum;
        }

        if (ProceduralNode.has_child("SplatMap")) {
            if (TryReadTerrainSplatMapDesc(ProceduralNode["SplatMap"], OutDesc.mSplatMapDesc) == false) {
                return false;
            }
        }

        return true;
    }

    bool TryReadTerrainBuildDesc(c4::yml::ConstNodeRef TerrainNode, const std::string& SceneName, Game::TerrainBuildDesc& OutDesc) {
        if (TerrainNode.readable() == false || TerrainNode.is_map() == false) {
            return false;
        }

        if (TerrainNode.has_child("HeightSourceType")) {
            std::string HeightSourceTypeText{};
            TerrainNode["HeightSourceType"] >> HeightSourceTypeText;
            if (TryParseTerrainHeightSourceTypeText(HeightSourceTypeText, OutDesc.mHeightSourceType) == false) {
                return false;
            }
        }
        else if (TerrainNode.has_child("HeightSource")) {
            std::string HeightSourceTypeText{};
            TerrainNode["HeightSource"] >> HeightSourceTypeText;
            if (TryParseTerrainHeightSourceTypeText(HeightSourceTypeText, OutDesc.mHeightSourceType) == false) {
                return false;
            }
        }

        if (TerrainNode.has_child("ProceduralHeightField")) {
            const bool IsProceduralDescRead{ TryReadTerrainProceduralHeightFieldDesc(TerrainNode["ProceduralHeightField"], OutDesc.mProceduralHeightFieldDesc) };
            if (IsProceduralDescRead == false) {
                return false;
            }

            if (TerrainNode.has_child("HeightSourceType") == false && TerrainNode.has_child("HeightSource") == false && TerrainNode.has_child("HeightMapPath") == false) {
                OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
            }
        }

        if (TerrainNode.has_child("ProceduralHeightFieldPath")) {
            std::string ProceduralHeightFieldPath{};
            TerrainNode["ProceduralHeightFieldPath"] >> ProceduralHeightFieldPath;
            if (ProceduralHeightFieldPath.empty()) {
                return false;
            }

            OutDesc.mProceduralHeightFieldPath = ResolveSceneResourcePath(SceneName, ProceduralHeightFieldPath);
            OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
        }

        if (TerrainNode.has_child("HeightMapPath")) {
            std::string HeightMapPath{};
            TerrainNode["HeightMapPath"] >> HeightMapPath;
            if (HeightMapPath.empty()) {
                return false;
            }

            OutDesc.HeightMapPath = ResolveSceneResourcePath(SceneName, HeightMapPath);
        }

        if (OutDesc.mHeightSourceType == Game::TerrainHeightSourceType::HeightMap && OutDesc.HeightMapPath.empty() == true) {
            return false;
        }

        if (TerrainNode.has_child("MaxHeight")) {
            TerrainNode["MaxHeight"] >> OutDesc.MaxHeight;
        }

        if (TerrainNode.has_child("CellSizeX")) {
            TerrainNode["CellSizeX"] >> OutDesc.CellSizeX;
        }

        if (TerrainNode.has_child("CellSizeZ")) {
            TerrainNode["CellSizeZ"] >> OutDesc.CellSizeZ;
        }

        if (TerrainNode.has_child("FlipV")) {
            std::string ValueText{};
            TerrainNode["FlipV"] >> ValueText;
            bool Value{};
            if (TryParseYamlBoolText(ValueText, Value) == false) {
                return false;
            }

            OutDesc.FlipV = Value;
        }

        if (TerrainNode.has_child("CenterOrigin")) {
            std::string ValueText{};
            TerrainNode["CenterOrigin"] >> ValueText;
            bool Value{};
            if (TryParseYamlBoolText(ValueText, Value) == false) {
                return false;
            }

            OutDesc.CenterOrigin = Value;
        }

        if (TerrainNode.has_child("TileQuadCount")) {
            TerrainNode["TileQuadCount"] >> OutDesc.TileQuadCount;
        }

        if (TerrainNode.has_child("LodCount")) {
            TerrainNode["LodCount"] >> OutDesc.LodCount;
        }

        if (TerrainNode.has_child("LodDistances")) {
            const c4::yml::ConstNodeRef LodDistancesNode{ TerrainNode["LodDistances"] };
            if (LodDistancesNode.is_seq() == false) {
                return false;
            }

            OutDesc.LodDistances.clear();
            for (const c4::yml::ConstNodeRef LodDistanceNode : LodDistancesNode.children()) {
                float LodDistance{};
                LodDistanceNode >> LodDistance;
                OutDesc.LodDistances.push_back(LodDistance);
            }
        }

        if (TerrainNode.has_child("StreamingEnabled")) {
            std::string ValueText{};
            TerrainNode["StreamingEnabled"] >> ValueText;
            bool Value{};
            if (TryParseYamlBoolText(ValueText, Value) == false) {
                return false;
            }

            OutDesc.mStreamingEnabled = Value;
        }

        if (TerrainNode.has_child("StreamingGridStep")) {
            TerrainNode["StreamingGridStep"] >> OutDesc.mStreamingGridStep;
        }

        return true;
    }

    bool TryParseTerrainModelSelector(const std::string& Selector, Game::TerrainBuildDesc& OutDesc) {
        if (StartsWith(Selector, "terrain:") == false) {
            return false;
        }

        const std::string ParameterText{ Selector.substr(8) };
        if (ParameterText.empty()) {
            return false;
        }

        try {
            std::size_t CurrentStart{ 0 };
            while (CurrentStart < ParameterText.size()) {
                const std::size_t TokenEnd{ ParameterText.find(';', CurrentStart) };
                const std::size_t TokenLength{ TokenEnd == std::string::npos ? ParameterText.size() - CurrentStart : TokenEnd - CurrentStart };
                const std::string Token{ ParameterText.substr(CurrentStart, TokenLength) };
                const std::size_t EqualsIndex{ Token.find('=') };
                if (EqualsIndex == std::string::npos || EqualsIndex == 0 || EqualsIndex + 1 >= Token.size()) {
                    return false;
                }

                const std::string Key{ Token.substr(0, EqualsIndex) };
                const std::string Value{ Token.substr(EqualsIndex + 1) };
                if (Key == "HeightSourceType") {
                    if (TryParseTerrainHeightSourceTypeText(Value, OutDesc.mHeightSourceType) == false) {
                        return false;
                    }
                }
                else if (Key == "HeightMapPath") {
                    OutDesc.HeightMapPath = Value;
                }
                else if (Key == "ProceduralHeightFieldPath") {
                    OutDesc.mProceduralHeightFieldPath = Value;
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralWidth") {
                    OutDesc.mProceduralHeightFieldDesc.mWidth = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralHeight") {
                    OutDesc.mProceduralHeightFieldDesc.mHeight = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralSeed") {
                    OutDesc.mProceduralHeightFieldDesc.mSeed = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralUseRandomSeed") {
                    bool BoolValue{};
                    if (TryParseYamlBoolText(Value, BoolValue) == false) {
                        return false;
                    }

                    OutDesc.mProceduralHeightFieldDesc.mUseRandomSeed = BoolValue;
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralOctaveCount") {
                    OutDesc.mProceduralHeightFieldDesc.mOctaveCount = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralNoiseScale") {
                    OutDesc.mProceduralHeightFieldDesc.mNoiseScale = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralPersistence") {
                    OutDesc.mProceduralHeightFieldDesc.mPersistence = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralLacunarity") {
                    OutDesc.mProceduralHeightFieldDesc.mLacunarity = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralBaseHeight") {
                    OutDesc.mProceduralHeightFieldDesc.mBaseHeight = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralHeightAmplitude") {
                    OutDesc.mProceduralHeightFieldDesc.mHeightAmplitude = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralLodExponent") {
                    OutDesc.mProceduralHeightFieldDesc.mLodExponent = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralSmoothingPassCount") {
                    OutDesc.mProceduralHeightFieldDesc.mSmoothingPassCount = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralMinimumWidth") {
                    OutDesc.mProceduralHeightFieldDesc.mMinimumWidth = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralMinimumHeight") {
                    OutDesc.mProceduralHeightFieldDesc.mMinimumHeight = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralMaximumOctaveCount") {
                    OutDesc.mProceduralHeightFieldDesc.mMaximumOctaveCount = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralMaximumSmoothingPassCount") {
                    OutDesc.mProceduralHeightFieldDesc.mMaximumSmoothingPassCount = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralMinimumHeightValue") {
                    OutDesc.mProceduralHeightFieldDesc.mMinimumHeightValue = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralMaximumHeightValue") {
                    OutDesc.mProceduralHeightFieldDesc.mMaximumHeightValue = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralSampleScaleX") {
                    OutDesc.mProceduralHeightFieldDesc.mSampleScaleX = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralSampleScaleZ") {
                    OutDesc.mProceduralHeightFieldDesc.mSampleScaleZ = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralSampleOffsetX") {
                    OutDesc.mProceduralHeightFieldDesc.mSampleOffsetX = static_cast<std::int32_t>(std::stol(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralSampleOffsetZ") {
                    OutDesc.mProceduralHeightFieldDesc.mSampleOffsetZ = static_cast<std::int32_t>(std::stol(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralInitialFrequency") {
                    OutDesc.mProceduralHeightFieldDesc.mInitialFrequency = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralInitialAmplitude") {
                    OutDesc.mProceduralHeightFieldDesc.mInitialAmplitude = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralOctaveSeedStep") {
                    OutDesc.mProceduralHeightFieldDesc.mOctaveSeedStep = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralNoiseNormalizationScale") {
                    OutDesc.mProceduralHeightFieldDesc.mNoiseNormalizationScale = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralNoiseNormalizationBias") {
                    OutDesc.mProceduralHeightFieldDesc.mNoiseNormalizationBias = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralHashShiftA") {
                    OutDesc.mProceduralHeightFieldDesc.mHashShiftA = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralHashShiftB") {
                    OutDesc.mProceduralHeightFieldDesc.mHashShiftB = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralHashShiftC") {
                    OutDesc.mProceduralHeightFieldDesc.mHashShiftC = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralHashShiftLimitExclusive") {
                    OutDesc.mProceduralHeightFieldDesc.mHashShiftLimitExclusive = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralHashMultiplierA") {
                    OutDesc.mProceduralHeightFieldDesc.mHashMultiplierA = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralHashMultiplierB") {
                    OutDesc.mProceduralHeightFieldDesc.mHashMultiplierB = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralHashCoordinateOffsetX") {
                    OutDesc.mProceduralHeightFieldDesc.mHashCoordinateOffsetX = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralHashCoordinateOffsetZ") {
                    OutDesc.mProceduralHeightFieldDesc.mHashCoordinateOffsetZ = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralGradientDirectionCount") {
                    OutDesc.mProceduralHeightFieldDesc.mGradientDirectionCount = static_cast<std::uint32_t>(std::stoul(Value));
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralFadeCoefficientA") {
                    OutDesc.mProceduralHeightFieldDesc.mFadeCoefficientA = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralFadeCoefficientB") {
                    OutDesc.mProceduralHeightFieldDesc.mFadeCoefficientB = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralFadeCoefficientC") {
                    OutDesc.mProceduralHeightFieldDesc.mFadeCoefficientC = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralSmoothingCornerWeight") {
                    OutDesc.mProceduralHeightFieldDesc.mSmoothingCornerWeight = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralSmoothingEdgeWeight") {
                    OutDesc.mProceduralHeightFieldDesc.mSmoothingEdgeWeight = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralSmoothingCenterWeight") {
                    OutDesc.mProceduralHeightFieldDesc.mSmoothingCenterWeight = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "ProceduralSmoothingWeightSum") {
                    OutDesc.mProceduralHeightFieldDesc.mSmoothingWeightSum = std::stof(Value);
                    OutDesc.mHeightSourceType = Game::TerrainHeightSourceType::Procedural;
                }
                else if (Key == "MaxHeight") {
                    OutDesc.MaxHeight = std::stof(Value);
                }
                else if (Key == "CellSizeX") {
                    OutDesc.CellSizeX = std::stof(Value);
                }
                else if (Key == "CellSizeZ") {
                    OutDesc.CellSizeZ = std::stof(Value);
                }
                else if (Key == "FlipV") {
                    bool BoolValue{};
                    if (TryParseYamlBoolText(Value, BoolValue) == false) {
                        return false;
                    }

                    OutDesc.FlipV = BoolValue;
                }
                else if (Key == "CenterOrigin") {
                    bool BoolValue{};
                    if (TryParseYamlBoolText(Value, BoolValue) == false) {
                        return false;
                    }

                    OutDesc.CenterOrigin = BoolValue;
                }
                else if (Key == "TileQuadCount") {
                    OutDesc.TileQuadCount = static_cast<std::uint32_t>(std::stoul(Value));
                }
                else if (Key == "LodCount") {
                    OutDesc.LodCount = static_cast<std::uint32_t>(std::stoul(Value));
                }
                else if (Key == "LodDistances") {
                    std::vector<float> LodDistances{};
                    if (TryParseFloatListText(Value, LodDistances) == false) {
                        return false;
                    }

                    OutDesc.LodDistances = std::move(LodDistances);
                }
                else if (Key == "StreamingEnabled") {
                    bool BoolValue{};
                    if (TryParseYamlBoolText(Value, BoolValue) == false) {
                        return false;
                    }

                    OutDesc.mStreamingEnabled = BoolValue;
                }
                else if (Key == "StreamingGridStep") {
                    OutDesc.mStreamingGridStep = static_cast<std::uint32_t>(std::stoul(Value));
                }
                else {
                    return false;
                }

                if (TokenEnd == std::string::npos) {
                    break;
                }

                CurrentStart = TokenEnd + 1;
            }
        }
        catch (const std::exception&) {
            return false;
        }

        if (OutDesc.mHeightSourceType == Game::TerrainHeightSourceType::Procedural) {
            return true;
        }

        return OutDesc.HeightMapPath.empty() == false;
    }

    void AppendTerrainSplatMapDesc(std::ostringstream& Stream, std::size_t IndentLevel, const Game::TerrainProceduralHeightFieldDesc::TerrainSplatMapDesc& Desc) {
        if (Desc.mVariables.empty() == true && Desc.mLayers.empty() == true) {
            return;
        }

        AppendLine(Stream, IndentLevel, "SplatMap:");
        AppendLine(Stream, IndentLevel + 1, std::string{ "NormalizeWeights: " } + ToYamlBooleanText(Desc.mNormalizeWeights));
        AppendLine(Stream, IndentLevel + 1, std::string{ "FallbackLayerIndex: " } + std::to_string(Desc.mFallbackLayerIndex));
        AppendLine(Stream, IndentLevel + 1, std::string{ "MinimumWeightSum: " } + std::to_string(Desc.mMinimumWeightSum));

        if (Desc.mVariables.empty() == false) {
            AppendLine(Stream, IndentLevel + 1, "Variables:");
            for (const Game::TerrainProceduralHeightFieldDesc::TerrainSplatMapVariableDesc& VariableDesc : Desc.mVariables) {
                AppendLine(Stream, IndentLevel + 2, "- Name: " + ToYamlText(VariableDesc.mName));
                AppendLine(Stream, IndentLevel + 3, "Formula: " + ToYamlText(VariableDesc.mFormula));
            }
        }

        if (Desc.mLayers.empty() == false) {
            AppendLine(Stream, IndentLevel + 1, "Layers:");
            for (const Game::TerrainProceduralHeightFieldDesc::TerrainSplatMapLayerDesc& LayerDesc : Desc.mLayers) {
                AppendLine(Stream, IndentLevel + 2, "- Name: " + ToYamlText(LayerDesc.mName));
                AppendLine(Stream, IndentLevel + 3, "Formula: " + ToYamlText(LayerDesc.mFormula));
            }
        }
    }

    void AppendTerrainProceduralHeightFieldDesc(std::ostringstream& Stream, std::size_t IndentLevel, const Game::TerrainProceduralHeightFieldDesc& Desc) {
        AppendLine(Stream, IndentLevel, "ProceduralHeightField:");
        AppendLine(Stream, IndentLevel + 1, std::string{ "Width: " } + std::to_string(Desc.mWidth));
        AppendLine(Stream, IndentLevel + 1, std::string{ "Height: " } + std::to_string(Desc.mHeight));
        AppendLine(Stream, IndentLevel + 1, std::string{ "Seed: " } + std::to_string(Desc.mSeed));
        AppendLine(Stream, IndentLevel + 1, std::string{ "UseRandomSeed: " } + ToYamlBooleanText(Desc.mUseRandomSeed));
        AppendLine(Stream, IndentLevel + 1, std::string{ "OctaveCount: " } + std::to_string(Desc.mOctaveCount));
        AppendLine(Stream, IndentLevel + 1, std::string{ "NoiseScale: " } + std::to_string(Desc.mNoiseScale));
        AppendLine(Stream, IndentLevel + 1, std::string{ "Persistence: " } + std::to_string(Desc.mPersistence));
        AppendLine(Stream, IndentLevel + 1, std::string{ "Lacunarity: " } + std::to_string(Desc.mLacunarity));
        AppendLine(Stream, IndentLevel + 1, std::string{ "BaseHeight: " } + std::to_string(Desc.mBaseHeight));
        AppendLine(Stream, IndentLevel + 1, std::string{ "HeightAmplitude: " } + std::to_string(Desc.mHeightAmplitude));
        AppendLine(Stream, IndentLevel + 1, std::string{ "LodExponent: " } + std::to_string(Desc.mLodExponent));
        AppendLine(Stream, IndentLevel + 1, std::string{ "SmoothingPassCount: " } + std::to_string(Desc.mSmoothingPassCount));
        AppendLine(Stream, IndentLevel + 1, std::string{ "MinimumWidth: " } + std::to_string(Desc.mMinimumWidth));
        AppendLine(Stream, IndentLevel + 1, std::string{ "MinimumHeight: " } + std::to_string(Desc.mMinimumHeight));
        AppendLine(Stream, IndentLevel + 1, std::string{ "MaximumOctaveCount: " } + std::to_string(Desc.mMaximumOctaveCount));
        AppendLine(Stream, IndentLevel + 1, std::string{ "MaximumSmoothingPassCount: " } + std::to_string(Desc.mMaximumSmoothingPassCount));
        AppendLine(Stream, IndentLevel + 1, std::string{ "MinimumHeightValue: " } + std::to_string(Desc.mMinimumHeightValue));
        AppendLine(Stream, IndentLevel + 1, std::string{ "MaximumHeightValue: " } + std::to_string(Desc.mMaximumHeightValue));
        AppendLine(Stream, IndentLevel + 1, std::string{ "SampleScaleX: " } + std::to_string(Desc.mSampleScaleX));
        AppendLine(Stream, IndentLevel + 1, std::string{ "SampleScaleZ: " } + std::to_string(Desc.mSampleScaleZ));
        AppendLine(Stream, IndentLevel + 1, std::string{ "SampleOffsetX: " } + std::to_string(Desc.mSampleOffsetX));
        AppendLine(Stream, IndentLevel + 1, std::string{ "SampleOffsetZ: " } + std::to_string(Desc.mSampleOffsetZ));
        AppendLine(Stream, IndentLevel + 1, std::string{ "InitialFrequency: " } + std::to_string(Desc.mInitialFrequency));
        AppendLine(Stream, IndentLevel + 1, std::string{ "InitialAmplitude: " } + std::to_string(Desc.mInitialAmplitude));
        AppendLine(Stream, IndentLevel + 1, std::string{ "OctaveSeedStep: " } + std::to_string(Desc.mOctaveSeedStep));
        AppendLine(Stream, IndentLevel + 1, std::string{ "NoiseNormalizationScale: " } + std::to_string(Desc.mNoiseNormalizationScale));
        AppendLine(Stream, IndentLevel + 1, std::string{ "NoiseNormalizationBias: " } + std::to_string(Desc.mNoiseNormalizationBias));
        AppendLine(Stream, IndentLevel + 1, std::string{ "HashShiftA: " } + std::to_string(Desc.mHashShiftA));
        AppendLine(Stream, IndentLevel + 1, std::string{ "HashShiftB: " } + std::to_string(Desc.mHashShiftB));
        AppendLine(Stream, IndentLevel + 1, std::string{ "HashShiftC: " } + std::to_string(Desc.mHashShiftC));
        AppendLine(Stream, IndentLevel + 1, std::string{ "HashShiftLimitExclusive: " } + std::to_string(Desc.mHashShiftLimitExclusive));
        AppendLine(Stream, IndentLevel + 1, std::string{ "HashMultiplierA: " } + std::to_string(Desc.mHashMultiplierA));
        AppendLine(Stream, IndentLevel + 1, std::string{ "HashMultiplierB: " } + std::to_string(Desc.mHashMultiplierB));
        AppendLine(Stream, IndentLevel + 1, std::string{ "HashCoordinateOffsetX: " } + std::to_string(Desc.mHashCoordinateOffsetX));
        AppendLine(Stream, IndentLevel + 1, std::string{ "HashCoordinateOffsetZ: " } + std::to_string(Desc.mHashCoordinateOffsetZ));
        AppendLine(Stream, IndentLevel + 1, std::string{ "GradientDirectionCount: " } + std::to_string(Desc.mGradientDirectionCount));
        AppendLine(Stream, IndentLevel + 1, std::string{ "FadeCoefficientA: " } + std::to_string(Desc.mFadeCoefficientA));
        AppendLine(Stream, IndentLevel + 1, std::string{ "FadeCoefficientB: " } + std::to_string(Desc.mFadeCoefficientB));
        AppendLine(Stream, IndentLevel + 1, std::string{ "FadeCoefficientC: " } + std::to_string(Desc.mFadeCoefficientC));
        AppendLine(Stream, IndentLevel + 1, std::string{ "SmoothingCornerWeight: " } + std::to_string(Desc.mSmoothingCornerWeight));
        AppendLine(Stream, IndentLevel + 1, std::string{ "SmoothingEdgeWeight: " } + std::to_string(Desc.mSmoothingEdgeWeight));
        AppendLine(Stream, IndentLevel + 1, std::string{ "SmoothingCenterWeight: " } + std::to_string(Desc.mSmoothingCenterWeight));
        AppendLine(Stream, IndentLevel + 1, std::string{ "SmoothingWeightSum: " } + std::to_string(Desc.mSmoothingWeightSum));
        AppendTerrainSplatMapDesc(Stream, IndentLevel + 1, Desc.mSplatMapDesc);
    }

    void AppendTerrainBuildDesc(std::ostringstream& Stream, std::size_t IndentLevel, const std::string& SceneName, const Game::TerrainBuildDesc& Desc) {
        AppendLine(Stream, IndentLevel, std::string{ TerrainTypeName } + std::string{ ":" });
        AppendLine(Stream, IndentLevel + 1, std::string{ "HeightSourceType: " } + BuildTerrainHeightSourceTypeText(Desc.mHeightSourceType));
        if (Desc.mHeightSourceType == Game::TerrainHeightSourceType::HeightMap) {
            AppendLine(Stream, IndentLevel + 1, std::string{ "HeightMapPath: " } + ToYamlText(MakeSceneRelativeResourcePath(SceneName, Desc.HeightMapPath)));
        }
        else if (Desc.mProceduralHeightFieldPath.empty() == false) {
            AppendLine(Stream, IndentLevel + 1, std::string{ "ProceduralHeightFieldPath: " } + ToYamlText(MakeSceneRelativeResourcePath(SceneName, Desc.mProceduralHeightFieldPath)));
        }
        else {
            AppendTerrainProceduralHeightFieldDesc(Stream, IndentLevel + 1, Desc.mProceduralHeightFieldDesc);
        }

        AppendLine(Stream, IndentLevel + 1, std::string{ "MaxHeight: " } + std::to_string(Desc.MaxHeight));
        AppendLine(Stream, IndentLevel + 1, std::string{ "CellSizeX: " } + std::to_string(Desc.CellSizeX));
        AppendLine(Stream, IndentLevel + 1, std::string{ "CellSizeZ: " } + std::to_string(Desc.CellSizeZ));
        AppendLine(Stream, IndentLevel + 1, std::string{ "FlipV: " } + ToYamlBooleanText(Desc.FlipV));
        AppendLine(Stream, IndentLevel + 1, std::string{ "CenterOrigin: " } + ToYamlBooleanText(Desc.CenterOrigin));
        AppendLine(Stream, IndentLevel + 1, std::string{ "TileQuadCount: " } + std::to_string(Desc.TileQuadCount));
        AppendLine(Stream, IndentLevel + 1, std::string{ "LodCount: " } + std::to_string(Desc.LodCount));
        AppendLine(Stream, IndentLevel + 1, std::string{ "LodDistances: [" } + BuildFloatListText(Desc.LodDistances) + std::string{ "]" });
        AppendLine(Stream, IndentLevel + 1, std::string{ "StreamingEnabled: " } + ToYamlBooleanText(Desc.mStreamingEnabled));
        AppendLine(Stream, IndentLevel + 1, std::string{ "StreamingGridStep: " } + std::to_string(Desc.mStreamingGridStep));
    }
}
