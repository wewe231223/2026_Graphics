#include "SceneEntityFactory.h"
#include <vector>
#include "Game/Scene/Legacy/Scene.h"
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/Bone.h"
#include "Game/Scene/Components/BoneSkinReference.h"
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/Culling.h"
#include "Game/Scene/Components/EntityHierarchy.h"
#include "Game/Scene/Components/Material.h"
#include "Game/Scene/Components/Name.h"
#include "Game/Scene/Components/PrefabInstance.h"
#include "Game/Scene/Components/SkinnedMeshRenderer.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Game/Scene/Components/Transform.h"

namespace Game {
    ModelHierarchySpawnRequest::ModelHierarchySpawnRequest() = default;
    ModelHierarchySpawnRequest::~ModelHierarchySpawnRequest() = default;
    ModelHierarchySpawnRequest::ModelHierarchySpawnRequest(const ModelHierarchySpawnRequest& Other) = default;
    ModelHierarchySpawnRequest& ModelHierarchySpawnRequest::operator=(const ModelHierarchySpawnRequest& Other) = default;
    ModelHierarchySpawnRequest::ModelHierarchySpawnRequest(ModelHierarchySpawnRequest&& Other) noexcept = default;
    ModelHierarchySpawnRequest& ModelHierarchySpawnRequest::operator=(ModelHierarchySpawnRequest&& Other) noexcept = default;

    SceneEntityFactory::SceneEntityFactory(Scene& TargetScene)
        : SceneEntityFactory{ TargetScene.GetWorld() } {
    }

    SceneEntityFactory::SceneEntityFactory(Arche::World& TargetWorld)
        : mWorld{ &TargetWorld } {
    }

    SceneEntityFactory::~SceneEntityFactory() = default;
    SceneEntityFactory::SceneEntityFactory(const SceneEntityFactory& Other) = default;
    SceneEntityFactory& SceneEntityFactory::operator=(const SceneEntityFactory& Other) = default;
    SceneEntityFactory::SceneEntityFactory(SceneEntityFactory&& Other) noexcept = default;
    SceneEntityFactory& SceneEntityFactory::operator=(SceneEntityFactory&& Other) noexcept = default;

    Arche::EntityID SceneEntityFactory::CreateEntity(bool IsDerivedEntity) {
        Arche::EntityID EntityId{ mWorld->CreateEntity() };
        EntityId.SetDerivedEntity(IsDerivedEntity);
        EnsureHierarchy(EntityId);
        return EntityId;
    }

    void SceneEntityFactory::EnsureHierarchy(Arche::EntityID EntityId) {
        EntityHierarchy* ExistingHierarchy{ mWorld->GetComponent<EntityHierarchy>(EntityId) };
        if (ExistingHierarchy != nullptr) {
            ExistingHierarchy->self = EntityId;
            return;
        }

        EntityHierarchy NewHierarchy{};
        NewHierarchy.self = EntityId;
        mWorld->AddComponent(EntityId, NewHierarchy);
    }

    void SceneEntityFactory::AttachChildEntity(Arche::EntityID ParentEntityId, Arche::EntityID ChildEntityId) {
        EntityHierarchy* ParentHierarchy{ mWorld->GetComponent<EntityHierarchy>(ParentEntityId) };
        EntityHierarchy* ChildHierarchy{ mWorld->GetComponent<EntityHierarchy>(ChildEntityId) };
        if (ParentHierarchy == nullptr || ChildHierarchy == nullptr) {
            return;
        }

        ChildHierarchy->parent = ParentEntityId;
        ChildHierarchy->nextSibling = Arche::NullEntityID;
        if (ParentHierarchy->firstChild == Arche::NullEntityID) {
            ParentHierarchy->firstChild = ChildEntityId;
            return;
        }

        Arche::EntityID SiblingEntityId{ ParentHierarchy->firstChild };
        while (SiblingEntityId != Arche::NullEntityID) {
            EntityHierarchy* SiblingHierarchy{ mWorld->GetComponent<EntityHierarchy>(SiblingEntityId) };
            if (SiblingHierarchy == nullptr) {
                return;
            }

            if (SiblingHierarchy->nextSibling == Arche::NullEntityID) {
                SiblingHierarchy->nextSibling = ChildEntityId;
                return;
            }

            SiblingEntityId = SiblingHierarchy->nextSibling;
        }
    }

    bool SceneEntityFactory::SpawnModelHierarchy(const ModelHierarchySpawnRequest& SpawnRequest, std::vector<Arche::EntityID>* OutNodeEntities) {
        if (SpawnRequest.ModelData == nullptr) {
            return false;
        }

        const Model* SourceModel{ SpawnRequest.ModelData.get() };
        const std::vector<ModelNode>& ModelNodes{ SourceModel->GetNodes() };
        const ModelNode* RootNode{ SourceModel->GetRootNode() };
        if (RootNode == nullptr || ModelNodes.empty()) {
            return false;
        }

        const std::size_t RootNodeIndex{ static_cast<std::size_t>(RootNode - ModelNodes.data()) };
        std::vector<Arche::EntityID> NodeEntities(ModelNodes.size(), Arche::NullEntityID);
        const bool HasRootEntityId{ SpawnRequest.RootEntityId.has_value() == true };
        NodeEntities[RootNodeIndex] = HasRootEntityId == true ? *SpawnRequest.RootEntityId : CreateEntity(SpawnRequest.IsDerivedEntity);
        EnsureHierarchy(NodeEntities[RootNodeIndex]);

        bool HasSkinnedMeshNode{};
        Arche::EntityID RootBoneReferenceEntityId{ Arche::NullEntityID };
        for (std::size_t NodeIndex{ 0 }; NodeIndex < ModelNodes.size(); ++NodeIndex) {
            if (NodeIndex == RootNodeIndex) {
                continue;
            }

            NodeEntities[NodeIndex] = CreateEntity(true);
        }

        for (std::size_t NodeIndex{ 0 }; NodeIndex < ModelNodes.size(); ++NodeIndex) {
            Transform NodeTransform{};
            NodeTransform.nodeToParent = ModelNodes[NodeIndex].GetNodeToParent();
            AddTransform(NodeEntities[NodeIndex], NodeTransform, NodeIndex == RootNodeIndex);

            const bool HasRenderableGeometry{ ModelNodes[NodeIndex].GetSubMeshes().empty() == false };
            if (HasRenderableGeometry == true) {
                if (ModelNodes[NodeIndex].IsSkinnedMesh() == true) {
                    SkinnedMeshRenderer NodeRenderer{};
                    NodeRenderer.model = SpawnRequest.ModelData.get();
                    NodeRenderer.nodeIndex = static_cast<std::uint32_t>(NodeIndex);
                    NodeRenderer.active = SpawnRequest.IsActive;
                    AddSkinnedMeshRenderer(NodeEntities[NodeIndex], NodeRenderer);
                }
                else {
                    StaticMeshRenderer NodeRenderer{};
                    NodeRenderer.model = SpawnRequest.ModelData.get();
                    NodeRenderer.nodeIndex = static_cast<std::uint32_t>(NodeIndex);
                    NodeRenderer.active = SpawnRequest.IsActive;
                    AddStaticMeshRenderer(NodeEntities[NodeIndex], NodeRenderer);

                    Culling NodeCulling{};
                    NodeCulling.frustumCulling = SpawnRequest.FrustumCullingEnabled;
                    AddCulling(NodeEntities[NodeIndex], NodeCulling);
                }

                Material NodeMaterial{};
                NodeMaterial.MaterialGroupIndex = SpawnRequest.MaterialGroupIndex;
                AddMaterial(NodeEntities[NodeIndex], NodeMaterial);

                BoundingBox NodeBoundingBox{};
                NodeBoundingBox.UpdateFromModel(SpawnRequest.ModelData.get(), static_cast<std::uint32_t>(NodeIndex));
                AddBoundingBox(NodeEntities[NodeIndex], NodeBoundingBox);
            }

            std::uint32_t RuntimeBoneInfoOffset{ 0 };
            std::uint32_t RuntimeBoneInfoCount{ 0 };
            const bool HasRuntimeBoneInfos{ SourceModel->TryGetRuntimeBoneInfoRange(static_cast<std::uint32_t>(NodeIndex), RuntimeBoneInfoOffset, RuntimeBoneInfoCount) };
            if (HasRuntimeBoneInfos == true) {
                Bone NodeBone{};
                NodeBone.model = SpawnRequest.ModelData.get();
                NodeBone.nodeIndex = static_cast<std::uint32_t>(NodeIndex);
                NodeBone.runtimeBoneInfoOffset = RuntimeBoneInfoOffset;
                NodeBone.runtimeBoneInfoCount = RuntimeBoneInfoCount;
                AddBone(NodeEntities[NodeIndex], NodeBone);

                BoundingBox NodeBoundingBox{};
                NodeBoundingBox.UpdateFromModel(SpawnRequest.ModelData.get(), static_cast<std::uint32_t>(NodeIndex));
                AddBoundingBox(NodeEntities[NodeIndex], NodeBoundingBox);
            }

            if (ModelNodes[NodeIndex].IsSkinnedMesh() == true) {
                HasSkinnedMeshNode = true;
                const Arche::EntityID BoneRootEntityId{ ResolveBoneRootEntityId(*SourceModel, ModelNodes[NodeIndex], NodeEntities) };
                if (RootBoneReferenceEntityId == Arche::NullEntityID) {
                    RootBoneReferenceEntityId = BoneRootEntityId;
                }
            }

            EnsureHierarchy(NodeEntities[NodeIndex]);

            const Name NodeName{ CreateNameComponent(ResolveNodeName(ModelNodes[NodeIndex], NodeIndex, RootNodeIndex, SpawnRequest.RootEntityName)) };
            AddName(NodeEntities[NodeIndex], NodeName, NodeIndex == RootNodeIndex, NodeIndex == RootNodeIndex);
        }

        for (std::size_t NodeIndex{ 0 }; NodeIndex < ModelNodes.size(); ++NodeIndex) {
            const std::vector<std::uint32_t>& Children{ ModelNodes[NodeIndex].GetChildren() };
            for (std::uint32_t ChildNodeIndex : Children) {
                if (ChildNodeIndex >= NodeEntities.size()) {
                    continue;
                }

                AttachChildEntity(NodeEntities[NodeIndex], NodeEntities[ChildNodeIndex]);
            }
        }

        if (HasSkinnedMeshNode == true) {
            Animator RootAnimator{};
            RootAnimator.clipIndex = -1;
            AddAnimator(NodeEntities[RootNodeIndex], RootAnimator);

            BoneSkinReference RootBoneSkinReference{};
            RootBoneSkinReference.boneRootEntityId = RootBoneReferenceEntityId;
            AddBoneSkinReference(NodeEntities[RootNodeIndex], RootBoneSkinReference);

            BoundingBox RootBoundingBox{};
            RootBoundingBox.ResetToUnitCube();
            AddBoundingBox(NodeEntities[RootNodeIndex], RootBoundingBox);

        }

        if (OutNodeEntities != nullptr) {
            *OutNodeEntities = std::move(NodeEntities);
        }

        return true;
    }

    void SceneEntityFactory::AddTransform(Arche::EntityID EntityId, const Transform& TransformComponent, bool ReplaceExistingRootComponent) {
        Transform* ExistingTransform{ mWorld->GetComponent<Transform>(EntityId) };
        if (ExistingTransform == nullptr) {
            mWorld->AddComponent(EntityId, TransformComponent);
            return;
        }

        if (ReplaceExistingRootComponent == true) {
            ExistingTransform->nodeToParent = TransformComponent.nodeToParent;
        }
    }

    void SceneEntityFactory::AddName(Arche::EntityID EntityId, const Name& NameComponent, bool ReplaceExistingRootComponent, bool OnlyWhenEmpty) {
        Name* ExistingName{ mWorld->GetComponent<Name>(EntityId) };
        if (ExistingName == nullptr) {
            mWorld->AddComponent(EntityId, NameComponent);
            return;
        }

        if (OnlyWhenEmpty == true) {
            if (GetNameText(*ExistingName)[0] == '\0') {
                *ExistingName = NameComponent;
            }
            return;
        }

        if (ReplaceExistingRootComponent == true) {
            *ExistingName = NameComponent;
        }
    }

    void SceneEntityFactory::AddStaticMeshRenderer(Arche::EntityID EntityId, const StaticMeshRenderer& StaticMeshRendererComponent) {
        StaticMeshRenderer* ExistingRenderer{ mWorld->GetComponent<StaticMeshRenderer>(EntityId) };
        if (ExistingRenderer == nullptr) {
            mWorld->AddComponent(EntityId, StaticMeshRendererComponent);
            return;
        }

        *ExistingRenderer = StaticMeshRendererComponent;
    }

    void SceneEntityFactory::AddSkinnedMeshRenderer(Arche::EntityID EntityId, const SkinnedMeshRenderer& SkinnedMeshRendererComponent) {
        SkinnedMeshRenderer* ExistingRenderer{ mWorld->GetComponent<SkinnedMeshRenderer>(EntityId) };
        if (ExistingRenderer == nullptr) {
            mWorld->AddComponent(EntityId, SkinnedMeshRendererComponent);
            return;
        }

        *ExistingRenderer = SkinnedMeshRendererComponent;
    }

    void SceneEntityFactory::AddCulling(Arche::EntityID EntityId, const Culling& CullingComponent) {
        Culling* ExistingCulling{ mWorld->GetComponent<Culling>(EntityId) };
        if (ExistingCulling == nullptr) {
            mWorld->AddComponent(EntityId, CullingComponent);
            return;
        }

        *ExistingCulling = CullingComponent;
    }

    void SceneEntityFactory::AddMaterial(Arche::EntityID EntityId, const Material& MaterialComponent) {
        Material* ExistingMaterial{ mWorld->GetComponent<Material>(EntityId) };
        if (ExistingMaterial == nullptr) {
            mWorld->AddComponent(EntityId, MaterialComponent);
            return;
        }

        *ExistingMaterial = MaterialComponent;
    }

    void SceneEntityFactory::AddBoundingBox(Arche::EntityID EntityId, const BoundingBox& BoundingBoxComponent) {
        BoundingBox* ExistingBoundingBox{ mWorld->GetComponent<BoundingBox>(EntityId) };
        if (ExistingBoundingBox == nullptr) {
            mWorld->AddComponent(EntityId, BoundingBoxComponent);
            return;
        }

        *ExistingBoundingBox = BoundingBoxComponent;
    }

    void SceneEntityFactory::AddBone(Arche::EntityID EntityId, const Bone& BoneComponent) {
        Bone* ExistingBone{ mWorld->GetComponent<Bone>(EntityId) };
        if (ExistingBone == nullptr) {
            mWorld->AddComponent(EntityId, BoneComponent);
            return;
        }

        *ExistingBone = BoneComponent;
    }

    void SceneEntityFactory::AddBoneSkinReference(Arche::EntityID EntityId, const BoneSkinReference& BoneSkinReferenceComponent) {
        BoneSkinReference* ExistingBoneSkinReference{ mWorld->GetComponent<BoneSkinReference>(EntityId) };
        if (ExistingBoneSkinReference == nullptr) {
            mWorld->AddComponent(EntityId, BoneSkinReferenceComponent);
            return;
        }

        *ExistingBoneSkinReference = BoneSkinReferenceComponent;
    }

    void SceneEntityFactory::AddAnimator(Arche::EntityID EntityId, const Animator& AnimatorComponent) {
        Animator* ExistingAnimator{ mWorld->GetComponent<Animator>(EntityId) };
        if (ExistingAnimator == nullptr) {
            mWorld->AddComponent(EntityId, AnimatorComponent);
            return;
        }

        *ExistingAnimator = AnimatorComponent;
    }
    Arche::EntityID SceneEntityFactory::ResolveBoneRootEntityId(const Model& ModelData, const ModelNode& ModelNodeData, const std::vector<Arche::EntityID>& NodeEntities) const {
        const std::string& SkinBoneRootNodeName{ ModelNodeData.GetSkinBoneRootNodeName() };
        if (SkinBoneRootNodeName.empty() == true) {
            return Arche::NullEntityID;
        }

        const ModelNode* BoneRootNode{ ModelData.FindNodeByName(SkinBoneRootNodeName) };
        if (BoneRootNode == nullptr) {
            return Arche::NullEntityID;
        }

        std::uint32_t BoneRootNodeIndex{ 0 };
        if (ModelData.TryFindNodeIndexById(BoneRootNode->GetId(), BoneRootNodeIndex) == false) {
            return Arche::NullEntityID;
        }

        if (BoneRootNodeIndex >= NodeEntities.size()) {
            return Arche::NullEntityID;
        }

        return NodeEntities[BoneRootNodeIndex];
    }


    std::string SceneEntityFactory::ResolveNodeName(const ModelNode& SourceNode, std::size_t NodeIndex, std::size_t RootNodeIndex, const std::string& RootEntityName) const {
        std::string NodeNameText{ SourceNode.GetName() };
        if (NodeIndex == RootNodeIndex && RootEntityName.empty() == false) {
            NodeNameText = RootEntityName;
        }

        if (NodeNameText.empty() == true) {
            NodeNameText = std::string{ "DroppedModelNode_" } + std::to_string(NodeIndex);
        }

        return NodeNameText;
    }
}
