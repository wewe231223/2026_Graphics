#include "SceneYamlInternal.h"
#include "Game/Scene/Components/PrefabInstance.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"

namespace Game::SceneYaml {
    namespace {
        bool ValidateSnapshot(const SceneWorldSnapshot& TargetSnapshot, SceneYamlSaveResult& SaveResult, std::string& OutYamlText);
        void AppendSceneHeader(std::ostringstream& Stream, const SceneWorldSnapshot& TargetSnapshot);
        std::unordered_map<std::uint64_t, PrefabDescriptor> BuildPrefabDescriptors(const SceneWorldSnapshot& TargetSnapshot, const Arche::World::WorldReadOnlyView& ReadOnlyWorld, const AssetRegistry& AssetRegistryInstance);
        void AppendPrefabDescriptors(std::ostringstream& Stream, const SceneWorldSnapshot& TargetSnapshot, SceneYamlSaveResult& SaveResult, const std::unordered_map<std::uint64_t, PrefabDescriptor>& PrefabDescriptors);
        std::unordered_map<Arche::EntityID, std::uint32_t> BuildSerializedEntityIds(const SceneWorldSnapshot& TargetSnapshot);
        void WriteEntity(std::ostringstream& Stream, const SceneWorldSnapshot& TargetSnapshot, SceneYamlSaveResult& SaveResult, const Arche::World::WorldReadOnlyView& ReadOnlyWorld, const AssetRegistry& AssetRegistryInstance, const std::unordered_map<Arche::EntityID, std::uint32_t>& SerializedEntityIds, const SceneWorldSnapshot::SceneEntitySnapshot& EntitySnapshot);

        bool ValidateSnapshot(const SceneWorldSnapshot& TargetSnapshot, SceneYamlSaveResult& SaveResult, std::string& OutYamlText) {
            const Arche::World::WorldReadOnlyView* ReadOnlyWorld{ TargetSnapshot.GetReadOnlyWorld() };
            const AssetRegistry* AssetRegistryInstance{ TargetSnapshot.GetAssetRegistry() };

            if (ReadOnlyWorld == nullptr) {
                SaveResult.IsSuccess = false;
                SaveResult.UndecidedItems.push_back("Scene Snapshot ??ReadOnlyWorld 媛 諛붿씤?⑸릺???덉? ?딆뒿?덈떎.");
                OutYamlText.clear();
                return false;
            }

            if (AssetRegistryInstance == nullptr) {
                SaveResult.IsSuccess = false;
                SaveResult.UndecidedItems.push_back("Scene Snapshot ??AssetRegistry 媛 諛붿씤?⑸릺???덉? ?딆뒿?덈떎.");
                OutYamlText.clear();
                return false;
            }

            return true;
        }

        void AppendSceneHeader(std::ostringstream& Stream, const SceneWorldSnapshot& TargetSnapshot) {
            AppendLine(Stream, 0, std::string{ "SceneName: " } + ToYamlText(TargetSnapshot.GetSceneName()));
            if (TargetSnapshot.GetSystemNames().empty()) {
                AppendLine(Stream, 0, "Systems: []");
            }
            else {
                AppendLine(Stream, 0, "Systems:");

                for (const std::string& SystemName : TargetSnapshot.GetSystemNames()) {
                    AppendLine(Stream, 1, std::string{ "- Type: " } + ToYamlText(SystemName));
                }
            }
        }

        std::unordered_map<std::uint64_t, PrefabDescriptor> BuildPrefabDescriptors(const SceneWorldSnapshot& TargetSnapshot, const Arche::World::WorldReadOnlyView& ReadOnlyWorld, const AssetRegistry& AssetRegistryInstance) {
            std::unordered_map<std::uint64_t, PrefabDescriptor> PrefabDescriptors{};
            for (const SceneWorldSnapshot::SceneEntitySnapshot& EntitySnapshot : TargetSnapshot.GetEntities()) {
                const Arche::EntityID EntityId{ EntitySnapshot.mEntityId };
                if (EntityId.IsDerivedEntity()) {
                    continue;
                }

                const PrefabInstance* PrefabInstanceComponent{ ReadOnlyWorld.GetComponent<PrefabInstance>(EntityId) };
                if (PrefabInstanceComponent == nullptr || PrefabInstanceComponent->PrefabId == 0ull) {
                    continue;
                }

                PrefabDescriptor Descriptor{};
                Descriptor.mPrefabId = PrefabInstanceComponent->PrefabId;

                const StaticMeshRenderer* ResolvedRenderer{ nullptr };
                if (TryFindRendererInHierarchy(&ReadOnlyWorld, EntityId, ResolvedRenderer) == true && ResolvedRenderer != nullptr) {
                    const std::string ModelSelector{ AssetRegistryInstance.FindModelSelectorByPointer(ResolvedRenderer->model) };
                    Descriptor.mModelSelector = MakeSceneRelativeResourcePath(TargetSnapshot.GetSceneName(), ModelSelector);
                    Descriptor.mActive = ResolvedRenderer->active;
                }

                std::uint32_t ResolvedMaterialGroupIndex{ 0 };
                if (TryResolveMaterialGroupIndexInHierarchy(&ReadOnlyWorld, EntityId, ResolvedMaterialGroupIndex) == true) {
                    const std::string MaterialPath{ AssetRegistryInstance.FindMaterialGroupSourcePathByIndex(ResolvedMaterialGroupIndex) };
                    if (IsDefaultMaterialPath(MaterialPath) == false) {
                        Descriptor.mMaterialPath = MakeSceneRelativeResourcePath(TargetSnapshot.GetSceneName(), MaterialPath);
                    }
                }

                PrefabDescriptors[Descriptor.mPrefabId] = Descriptor;
            }

            return PrefabDescriptors;
        }

        void AppendPrefabDescriptors(std::ostringstream& Stream, const SceneWorldSnapshot& TargetSnapshot, SceneYamlSaveResult& SaveResult, const std::unordered_map<std::uint64_t, PrefabDescriptor>& PrefabDescriptors) {
            if (PrefabDescriptors.empty() == true) {
                return;
            }

            AppendLine(Stream, 0, "Prefabs:");
            for (const std::pair<const std::uint64_t, PrefabDescriptor>& PrefabPair : PrefabDescriptors) {
                const PrefabDescriptor& Descriptor{ PrefabPair.second };
                AppendLine(Stream, 1, std::string{ "- &Prefab" } + std::to_string(Descriptor.mPrefabId));
                AppendLine(Stream, 2, std::string{ "prefabId: " } + std::to_string(Descriptor.mPrefabId));
                if (Descriptor.mModelSelector.empty()) {
                    SaveResult.IsSuccess = false;
                    SaveResult.UndecidedItems.push_back(std::string{ "PrefabId ????묐릺??modelPath 瑜?李얠? 紐삵뻽?듬땲?? " } + std::to_string(Descriptor.mPrefabId));
                }

                AppendLine(Stream, 2, std::string{ "modelPath: " } + ToYamlText(Descriptor.mModelSelector));
                AppendLine(Stream, 2, std::string{ "materialPath: " } + ToYamlText(Descriptor.mMaterialPath));
                AppendLine(Stream, 2, std::string{ "active: " } + ToYamlBooleanText(Descriptor.mActive));
            }
        }

        std::unordered_map<Arche::EntityID, std::uint32_t> BuildSerializedEntityIds(const SceneWorldSnapshot& TargetSnapshot) {
            std::unordered_map<Arche::EntityID, std::uint32_t> SerializedEntityIds{};
            std::uint32_t NextSerializedEntityId{ 0 };
            for (const SceneWorldSnapshot::SceneEntitySnapshot& EntitySnapshot : TargetSnapshot.GetEntities()) {
                if (EntitySnapshot.mEntityId.IsDerivedEntity()) {
                    continue;
                }

                SerializedEntityIds[EntitySnapshot.mEntityId] = NextSerializedEntityId;
                NextSerializedEntityId += 1;
            }

            return SerializedEntityIds;
        }

        void WriteEntity(std::ostringstream& Stream, const SceneWorldSnapshot& TargetSnapshot, SceneYamlSaveResult& SaveResult, const Arche::World::WorldReadOnlyView& ReadOnlyWorld, const AssetRegistry& AssetRegistryInstance, const std::unordered_map<Arche::EntityID, std::uint32_t>& SerializedEntityIds, const SceneWorldSnapshot::SceneEntitySnapshot& EntitySnapshot) {
            const Arche::EntityID EntityId{ EntitySnapshot.mEntityId };
            if (EntityId.IsDerivedEntity()) {
                return;
            }

            const std::unordered_map<Arche::EntityID, std::uint32_t>::const_iterator SerializedEntityIter{ SerializedEntityIds.find(EntityId) };
            if (SerializedEntityIter == SerializedEntityIds.end()) {
                return;
            }

            AppendLine(Stream, 1, std::string{ "- EntityId: " } + std::to_string(SerializedEntityIter->second));
            const std::unordered_map<Arche::EntityID, std::uint32_t>::const_iterator SerializedParentIter{ SerializedEntityIds.find(EntitySnapshot.mParentId) };
            if (SerializedParentIter == SerializedEntityIds.end()) {
                AppendLine(Stream, 2, "ParentEntityId: -1");
            }
            else {
                AppendLine(Stream, 2, std::string{ "ParentEntityId: " } + std::to_string(SerializedParentIter->second));
            }

            const StaticMeshRenderer* StaticMeshRendererComponent{ ReadOnlyWorld.GetComponent<StaticMeshRenderer>(EntityId) };
            if (ShouldSkipEntityInSceneExport(StaticMeshRendererComponent)) {
                return;
            }

            AppendLine(Stream, 2, "Components:");
            const SceneYamlComponentWriteContext WriteContext{ Stream, SaveResult, TargetSnapshot, ReadOnlyWorld, AssetRegistryInstance, SerializedEntityIds, EntitySnapshot };
            for (const SceneYamlComponentWriter& ComponentWriter : GetSceneYamlComponentWriters()) {
                static_cast<void>(ComponentWriter.mTypeName);
                ComponentWriter.mWrite(WriteContext);
            }
        }
    }

    SceneYamlSaveResult SceneYamlWriter::Serialize(const SceneWorldSnapshot& TargetSnapshot, std::string& OutYamlText) const {
        SceneYamlSaveResult SaveResult{};
        if (ValidateSnapshot(TargetSnapshot, SaveResult, OutYamlText) == false) {
            return SaveResult;
        }

        std::ostringstream Stream{};
        const Arche::World::WorldReadOnlyView* ReadOnlyWorld{ TargetSnapshot.GetReadOnlyWorld() };
        const AssetRegistry* AssetRegistryInstance{ TargetSnapshot.GetAssetRegistry() };

        AppendSceneHeader(Stream, TargetSnapshot);
        const std::unordered_map<std::uint64_t, PrefabDescriptor> PrefabDescriptors{ BuildPrefabDescriptors(TargetSnapshot, *ReadOnlyWorld, *AssetRegistryInstance) };
        AppendPrefabDescriptors(Stream, TargetSnapshot, SaveResult, PrefabDescriptors);
        AppendLine(Stream, 0, "Entities:");

        const std::unordered_map<Arche::EntityID, std::uint32_t> SerializedEntityIds{ BuildSerializedEntityIds(TargetSnapshot) };
        for (const SceneWorldSnapshot::SceneEntitySnapshot& EntitySnapshot : TargetSnapshot.GetEntities()) {
            WriteEntity(Stream, TargetSnapshot, SaveResult, *ReadOnlyWorld, *AssetRegistryInstance, SerializedEntityIds, EntitySnapshot);
        }

        OutYamlText = Stream.str();
        return SaveResult;
    }
}
