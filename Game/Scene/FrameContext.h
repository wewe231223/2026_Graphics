#pragma once
#include <vector>
#include "Arche/Common.h"
#include "Game/Base/RenderFrameData.h"
#include "Game/Scene/SceneWorldSnapshot.h"
#include "Game/Terrain/TerrainManager.h"
#include "PhysicsLib/Common.h"
#include "PhysicsLib/Runtime/PhysicsRuntimeTypes.h"
#include "Utility/SimpleMathWrapper.h"

class PhysicsRuntime;

namespace Game {
    class AssetRegistry;
    struct RegisteredMaterialGroup;

    struct SkinnedMeshPreparedData final {
        Arche::EntityID EntityId{ Arche::NullEntityID };
        std::vector<SimpleMath::Matrix> BonePalette{};
        std::vector<RFD::BoundingBoxContext> BoneBoundingBoxContexts{};
    };

    struct FrameContext final {
        RFD::RenderFrameData RenderData{};
        const std::vector<RegisteredMaterialGroup>* MaterialGroups{ nullptr };
        AssetRegistry* AssetRegistryResource{ nullptr };
        IPhysicsWorld* PhysicsWorldResource{ nullptr };
        PhysicsRuntime* PhysicsRuntimeResource{ nullptr };
        const PhysicsSnapshot* PhysicsSnapshotResource{ nullptr };
        TerrainManager* TerrainManagerResource{ nullptr };
        ITerrainQuery* TerrainQueryResource{ nullptr };
        bool IsPhysicsRuntimeModeEnabled{};
        PhysicsRuntimeStatus PhysicsRuntimeStatus{};
        Arche::EntityID PickedEntityId{ Arche::NullEntityID };
        std::vector<SkinnedMeshPreparedData> SkinnedMeshPreparedDataItems{};
    };
}
