#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "Scene.h"
#include "Game/Model/Model.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/AnimatorRootBoneDeltaDebug.h"
#include "Game/Scene/Components/Name.h"
#include "Game/Scene/Components/PrefabInstance.h"
#include "Game/Scene/Components/SkinnedMeshRenderer.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"

namespace Game {
    struct ModelHierarchySpawnRequest final {
    public:
        ModelHierarchySpawnRequest();
        ~ModelHierarchySpawnRequest();
        ModelHierarchySpawnRequest(const ModelHierarchySpawnRequest& Other);
        ModelHierarchySpawnRequest& operator=(const ModelHierarchySpawnRequest& Other);
        ModelHierarchySpawnRequest(ModelHierarchySpawnRequest&& Other) noexcept;
        ModelHierarchySpawnRequest& operator=(ModelHierarchySpawnRequest&& Other) noexcept;

    public:
        std::shared_ptr<Model> ModelData{};
        std::optional<Arche::EntityID> RootEntityId{};
        std::string RootEntityName{};
        std::uint32_t MaterialGroupIndex{};
        bool IsActive{ true };
        bool IsDerivedEntity{ false };
    };

    class SceneEntityFactory final {
    public:
        SceneEntityFactory(Scene& TargetScene);
        ~SceneEntityFactory();
        SceneEntityFactory(const SceneEntityFactory& Other);
        SceneEntityFactory& operator=(const SceneEntityFactory& Other);
        SceneEntityFactory(SceneEntityFactory&& Other) noexcept;
        SceneEntityFactory& operator=(SceneEntityFactory&& Other) noexcept;

    public:
        Arche::EntityID CreateEntity(bool IsDerivedEntity);
        void EnsureHierarchy(Arche::EntityID EntityId);
        void AttachChildEntity(Arche::EntityID ParentEntityId, Arche::EntityID ChildEntityId);
        bool SpawnModelHierarchy(const ModelHierarchySpawnRequest& SpawnRequest, std::vector<Arche::EntityID>* OutNodeEntities = nullptr);

    private:
        void AddTransform(Arche::EntityID EntityId, const Transform& TransformComponent, bool ReplaceExistingRootComponent);
        void AddName(Arche::EntityID EntityId, const Name& NameComponent, bool ReplaceExistingRootComponent, bool OnlyWhenEmpty);
        void AddStaticMeshRenderer(Arche::EntityID EntityId, const StaticMeshRenderer& StaticMeshRendererComponent);
        void AddSkinnedMeshRenderer(Arche::EntityID EntityId, const SkinnedMeshRenderer& SkinnedMeshRendererComponent);
        void AddBoundingBox(Arche::EntityID EntityId, const BoundingBox& BoundingBoxComponent);
        void AddBone(Arche::EntityID EntityId, const Bone& BoneComponent);
        void AddBoneSkinReference(Arche::EntityID EntityId, const BoneSkinReference& BoneSkinReferenceComponent);
        void AddAnimator(Arche::EntityID EntityId, const Animator& AnimatorComponent);
        void AddAnimatorRootBoneDeltaDebug(Arche::EntityID EntityId, const AnimatorRootBoneDeltaDebug& AnimatorRootBoneDeltaDebugComponent);

        Arche::EntityID ResolveBoneRootEntityId(const Model& ModelData, const ModelNode& ModelNodeData, const std::vector<Arche::EntityID>& NodeEntities) const;
        std::string ResolveNodeName(const ModelNode& SourceNode, std::size_t NodeIndex, std::size_t RootNodeIndex, const std::string& RootEntityName) const;

    private:
        Scene* mScene{};
    };
}
