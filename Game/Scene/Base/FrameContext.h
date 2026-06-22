#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Arche/Common.h"
#include "RenderContract/Frame/RenderFrameData.h"
#include "Game/Environment/EnvironmentObjectRenderContext.h"
#include "Game/Scene/Base/SceneWorldSnapshot.h"
#include "Terrain/TerrainManager.h"
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
        std::vector<RenderContract::BoundingBoxContext> BoneBoundingBoxContexts{};
    };

    struct FramePipelineAssignment final {
    public:
        Arche::EntityID mUnitEntityId{ Arche::NullEntityID };
        std::string mPipelineName{};
    };

    struct FrameContext final {
        RenderContract::RenderFrameData RenderData{};
        EnvironmentObjectRenderContext mEnvironmentObjectRenderContext{};
        const std::vector<RegisteredMaterialGroup>* MaterialGroups{ nullptr };
        AssetRegistry* AssetRegistryResource{ nullptr };
        IPhysicsWorld* PhysicsWorldResource{ nullptr };
        PhysicsRuntime* PhysicsRuntimeResource{ nullptr };
        const PhysicsSnapshot* PhysicsSnapshotResource{ nullptr };
        const std::vector<Arche::EntityID>* PhysicsSynchronizationEntityIds{ nullptr };
        Terrain::TerrainManager* TerrainManagerResource{ nullptr };
        Terrain::ITerrainQuery* TerrainQueryResource{ nullptr };
        bool IsPhysicsRuntimeModeEnabled{};
        std::uint32_t mPhysicsWorldVersion{};
        PhysicsRuntimeStatus PhysicsRuntimeStatus{};
        Arche::EntityID PickedEntityId{ Arche::NullEntityID };
        std::vector<SkinnedMeshPreparedData> SkinnedMeshPreparedDataItems{};
        std::vector<FramePipelineAssignment> mRuntimePipelineAssignments{};
        std::uint64_t mRuntimePipelineAssignmentVersion{};
    };
}
