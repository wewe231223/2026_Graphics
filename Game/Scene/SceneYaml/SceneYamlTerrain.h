#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>
#include <ryml.hpp>
#include "Game/Model/TerrainHeightFieldFactory.h"
#include "Game/Model/TerrainMeshTypes.h"
#include "Game/Model/TerrainRenderResource.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/TerrainRenderer.h"
#include "Game/Scene/Components/Transform.h"
#include "PhysicsLib/Actors/PhysicsTerrainActor.h"
#include "SceneYamlTypes.h"

namespace Game::SceneYaml {
    std::string BuildTerrainHeightSourceTypeText(Game::TerrainHeightSourceType SourceType);
    bool TryParseTerrainHeightSourceTypeText(const std::string& ValueText, Game::TerrainHeightSourceType& OutSourceType);
    DirectX::SimpleMath::Matrix BuildTransformOnlyWorldMatrix(const Game::Transform& TransformComponent);
    DirectX::SimpleMath::Matrix BuildTransformOffsetMatrix(const Game::Transform& TransformComponent);
    std::vector<DirectX::SimpleMath::Vector2> BuildTerrainSnapSamplePoints(const Game::BoundingBox* BoundingBoxComponent, const Game::Transform& TransformComponent);
    float CalculateBottomOffsetY(const Game::BoundingBox* BoundingBoxComponent, const Game::Transform& TransformComponent);
    bool TryResolveHighestTerrainSurfaceHeight(const Arche::World& World, const std::vector<TerrainSurfaceBinding>& TerrainSurfaceBindings, const DirectX::SimpleMath::Vector3& Position, float& OutSurfaceHeight);
    bool TryResolveHighestTerrainSurfaceHeight(const Arche::World& World, const std::vector<TerrainSurfaceBinding>& TerrainSurfaceBindings, const std::vector<DirectX::SimpleMath::Vector2>& SamplePoints, float& OutSurfaceHeight);
    bool TryBuildTerrainActorDescFromHeightField(const Game::HeightFieldData& HeightFieldDataValue, const Game::TerrainBuildDesc& TerrainBuildDescValue, PhysicsTerrainActor::ActorDesc& OutActorDesc);
    bool TryBuildTerrainActorDescFromRenderResource(const Game::TerrainRenderResource& TerrainResource, PhysicsTerrainActor::ActorDesc& OutActorDesc);
    std::uint32_t ResolveTerrainStreamingGridStep(const Game::TerrainBuildDesc& TerrainBuildDescValue);
    std::int32_t FloorTerrainStreamingGridToStep(std::int32_t Value, std::uint32_t Step);
    std::int32_t CalculateTerrainStreamingOriginGrid(float FocusPosition, float CellSize, std::uint32_t HeightFieldVertexCount, std::uint32_t TileQuadCount, std::uint32_t Step);
    float CalculateTerrainStreamingWorldOrigin(std::int32_t OriginGrid, std::uint32_t HeightFieldVertexCount, float CellSize);
    bool TryResolveTerrainStreamingFocusPosition(Arche::World& World, DirectX::SimpleMath::Vector3& OutFocusPosition);
    bool TryPrepareInitialStreamingTerrainBuildDesc(Arche::World& World, Game::TerrainBuildDesc& InOutTerrainBuildDesc);
    void ApplyInitialStreamingTerrainTransform(const Game::TerrainRenderResource& TerrainResource, Game::Transform* TerrainTransformComponent);
    void ApplyPendingTerrainSnapBindings(Arche::World& World, const std::vector<TerrainSurfaceBinding>& TerrainSurfaceBindings, const std::vector<PendingTerrainSnapBinding>& PendingTerrainSnapBindings);
    bool TryReadTerrainSplatMapExpressionEntry(c4::yml::ConstNodeRef EntryNode, std::string& OutName, std::vector<float>& OutParameters);
    bool TryReadTerrainSplatMapDesc(c4::yml::ConstNodeRef SplatMapNode, Game::TerrainProceduralHeightFieldDesc::TerrainSplatMapDesc& OutSplatMapDesc);
    bool TryReadTerrainProceduralHeightFieldDesc(c4::yml::ConstNodeRef ProceduralNode, Game::TerrainProceduralHeightFieldDesc& OutDesc);
    bool TryReadTerrainBuildDesc(c4::yml::ConstNodeRef TerrainNode, const std::string& SceneName, Game::TerrainBuildDesc& OutDesc);
    bool TryParseTerrainModelSelector(const std::string& Selector, Game::TerrainBuildDesc& OutDesc);
    void AppendTerrainSplatMapDesc(std::ostringstream& Stream, std::size_t IndentLevel, const Game::TerrainProceduralHeightFieldDesc::TerrainSplatMapDesc& SplatMapDesc);
    void AppendTerrainProceduralHeightFieldDesc(std::ostringstream& Stream, std::size_t IndentLevel, const Game::TerrainProceduralHeightFieldDesc& Desc);
    void AppendTerrainBuildDesc(std::ostringstream& Stream, std::size_t IndentLevel, const std::string& SceneName, const Game::TerrainBuildDesc& Desc);
}
