#include "SceneYamlTerrainComponent.h"
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "Game/Model/TerrainRenderResource.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/Culling.h"
#include "Game/Scene/Components/Material.h"
#include "Game/Scene/Components/Name.h"
#include "Game/Scene/Components/PrefabInstance.h"
#include "Game/Scene/Components/TerrainRenderer.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/Base/SceneEntityFactory.h"
#include "Game/Scene/SceneYaml/SceneYamlReadUtils.h"
#include "Game/Scene/SceneYaml/SceneYamlTerrain.h"
#include "Game/Scene/SceneYaml/SceneYamlWriteUtils.h"

namespace Game::SceneYaml {
    const char* SceneYamlTerrainComponentReader::TypeName() {
        return TerrainTypeName;
    }

    void SceneYamlTerrainComponentReader::Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState) {
        if (ComponentsNode.has_child(TypeName()) == false || ReadState.mHasInstantiatedPrefabModel == true) {
            return;
        }

        const c4::yml::ConstNodeRef TerrainNode{ ComponentsNode[TypeName()] };
        Terrain::TerrainBuildDesc Desc{};
        const bool IsTerrainDescRead{ TryReadTerrainBuildDesc(TerrainNode, LoadContext.mSceneName, Desc) };
        if (IsTerrainDescRead == false) {
            LoadContext.mLoadResult.IsSuccess = false;
            LoadContext.mLoadResult.UndecidedItems.push_back("Terrain Component 데이터 해석 실패");
            return;
        }

        const bool IsInitialStreamingTerrainDescPrepared{ TryPrepareInitialStreamingTerrainBuildDesc(LoadContext.mScene.GetWorld(), Desc) };
        if (IsInitialStreamingTerrainDescPrepared == false) {
            LoadContext.mLoadResult.IsSuccess = false;
            LoadContext.mLoadResult.UndecidedItems.push_back("Terrain Streaming 초기 데이터 해석 실패");
            ReadState.mShouldStopReadingEntity = true;
            return;
        }

        const std::shared_ptr<TerrainRenderResource> TerrainResource{ LoadContext.mScene.GetAssetRegistry().GetTerrainRenderResource(Desc) };
        if (TerrainResource == nullptr) {
            LoadContext.mLoadResult.IsSuccess = false;
            LoadContext.mLoadResult.UndecidedItems.push_back(std::string{ "Terrain 생성 실패: " } + (Desc.mHeightSourceType == Terrain::TerrainHeightSourceType::Procedural ? std::string{ "Procedural" } : Desc.HeightMapPath));
            return;
        }

        bool IsActive{ true };
        if (TerrainNode.has_child("active")) {
            TerrainNode["active"] >> IsActive;
        }

        Transform* TerrainTransformComponent{ LoadContext.mScene.GetWorld().GetComponent<Transform>(Entity) };
        ApplyInitialStreamingTerrainTransform(*TerrainResource, TerrainTransformComponent);

        PhysicsTerrainActor::ActorDesc TerrainActorDesc{};
        const bool IsTerrainActorDescBuilt{ TryBuildTerrainActorDescFromRenderResource(*TerrainResource, TerrainActorDesc) };
        if (IsTerrainActorDescBuilt == false) {
            LoadContext.mLoadResult.IsSuccess = false;
            LoadContext.mLoadResult.UndecidedItems.push_back("Terrain ActorDesc 생성 실패");
            ReadState.mShouldStopReadingEntity = true;
            return;
        }

        TerrainRenderer TerrainRendererComponent{};
        TerrainRendererComponent.mResource = TerrainResource.get();
        TerrainRendererComponent.mTileQuadCount = Desc.TileQuadCount;
        TerrainRendererComponent.mActive = IsActive;
        LoadContext.mScene.GetWorld().AddComponent(Entity, TerrainRendererComponent);

        BoundingBox TerrainBoundingBox{};
        TerrainBoundingBox.SetObb(TerrainResource->GetLocalBoundingBox());
        BoundingBox* ExistingTerrainBoundingBox{ LoadContext.mScene.GetWorld().GetComponent<BoundingBox>(Entity) };
        if (ExistingTerrainBoundingBox == nullptr) {
            LoadContext.mScene.GetWorld().AddComponent(Entity, TerrainBoundingBox);
        }
        else {
            ExistingTerrainBoundingBox->SetObb(TerrainResource->GetLocalBoundingBox());
        }

        Culling TerrainCulling{};
        TerrainCulling.frustumCulling = ReadState.mFrustumCullingEnabled;
        Culling* ExistingTerrainCulling{ LoadContext.mScene.GetWorld().GetComponent<Culling>(Entity) };
        if (ExistingTerrainCulling == nullptr) {
            LoadContext.mScene.GetWorld().AddComponent(Entity, TerrainCulling);
        }
        else {
            ExistingTerrainCulling->frustumCulling = ReadState.mFrustumCullingEnabled;
        }

        const Material* ParentTerrainMaterial{ LoadContext.mScene.GetWorld().GetComponent<Material>(Entity) };
        const std::vector<Terrain::TerrainTileMetadata>& TileMetadataItems{ TerrainResource->GetTileMetadata() };
        for (std::size_t TileMetadataIndex{ 0 }; TileMetadataIndex < TileMetadataItems.size(); ++TileMetadataIndex) {
            const Terrain::TerrainTileMetadata& TileMetadata{ TileMetadataItems[TileMetadataIndex] };
            const Arche::EntityID TileEntity{ LoadContext.mEntityFactory.CreateEntity(true) };
            LoadContext.mEntityFactory.AttachChildEntity(Entity, TileEntity);

            const Name TileName{ CreateNameComponent(std::format("Terrain_Tile_{}_{}", TileMetadata.mTileIndexX, TileMetadata.mTileIndexZ)) };
            LoadContext.mScene.GetWorld().AddComponent(TileEntity, TileName);

            TerrainRenderer TileTerrainRenderer{};
            TileTerrainRenderer.mResource = TerrainResource.get();
            TileTerrainRenderer.mTileQuadCount = Desc.TileQuadCount;
            TileTerrainRenderer.mTileMetadataIndex = static_cast<std::uint32_t>(TileMetadataIndex);
            TileTerrainRenderer.mActive = IsActive;
            LoadContext.mScene.GetWorld().AddComponent(TileEntity, TileTerrainRenderer);

            Material TileMaterial{};
            if (ParentTerrainMaterial == nullptr) {
                TileMaterial.MaterialGroupIndex = ReadState.mMaterialGroupIndexForModel;
            }
            else {
                TileMaterial = *ParentTerrainMaterial;
            }
            LoadContext.mScene.GetWorld().AddComponent(TileEntity, TileMaterial);

            Culling TileCulling{};
            TileCulling.frustumCulling = ReadState.mFrustumCullingEnabled;
            LoadContext.mScene.GetWorld().AddComponent(TileEntity, TileCulling);

            BoundingBox TileBoundingBox{};
            TileBoundingBox.SetObb(TileMetadata.mLocalBoundingBox);
            LoadContext.mScene.GetWorld().AddComponent(TileEntity, TileBoundingBox);
        }

        LoadContext.mScene.AddTerrainActorDesc(Entity, TerrainActorDesc);
        TerrainSurfaceBinding NewTerrainSurfaceBinding{};
        NewTerrainSurfaceBinding.mEntityId = Entity;
        NewTerrainSurfaceBinding.mTerrainActorDesc = TerrainActorDesc;
        LoadContext.mTerrainSurfaceBindings.push_back(std::move(NewTerrainSurfaceBinding));
    }

    const char* SceneYamlTerrainComponentWriter::TypeName() {
        return TerrainTypeName;
    }

    void SceneYamlTerrainComponentWriter::Write(const SceneYamlComponentWriteContext& WriteContext) {
        const Arche::EntityID EntityId{ WriteContext.mEntitySnapshot.mEntityId };
        const TerrainRenderer* TerrainRendererComponent{ WriteContext.mReadOnlyWorld.GetComponent<TerrainRenderer>(EntityId) };
        const PrefabInstance* PrefabInstanceComponent{ WriteContext.mReadOnlyWorld.GetComponent<PrefabInstance>(EntityId) };
        if (TerrainRendererComponent == nullptr || PrefabInstanceComponent != nullptr) {
            return;
        }

        const std::string TerrainSelector{ WriteContext.mAssetRegistry.FindTerrainRenderResourceSelectorByPointer(TerrainRendererComponent->mResource) };
        if (TerrainSelector.empty()) {
            WriteContext.mSaveResult.IsSuccess = false;
            WriteContext.mSaveResult.UndecidedItems.push_back("TerrainRenderer resource 포인터에 대응되는 selector 를 찾지 못했습니다.");
            return;
        }

        Terrain::TerrainBuildDesc TerrainDesc{};
        const bool IsTerrainResource{ TryParseTerrainModelSelector(TerrainSelector, TerrainDesc) };
        if (IsTerrainResource == true) {
            AppendTerrainBuildDesc(WriteContext.mStream, 3, WriteContext.mTargetSnapshot.GetSceneName(), TerrainDesc);
            AppendLine(WriteContext.mStream, 4, std::string{ "active: " } + ToYamlBooleanText(TerrainRendererComponent->mActive));
        }
        else {
            WriteContext.mSaveResult.IsSuccess = false;
            WriteContext.mSaveResult.UndecidedItems.push_back("TerrainRenderer selector 해석 실패");
        }
    }
}
