#include "SceneWorldSnapshot.h"

#include <unordered_map>
#include <utility>

#include "Game/Model/AssetRegistry.h"

namespace Game {
    SceneWorldSnapshot::SceneWorldSnapshot()
        : mReadOnlyWorld{}
        , mWorld{}
        , mAssetRegistry{}
        , mSceneName{}
        , mSystemNames{}
        , mEntities{}
        , mRootIndices{}
        , mChildIndices{} {
    }

    SceneWorldSnapshot::~SceneWorldSnapshot() {
    }

    SceneWorldSnapshot::SceneWorldSnapshot(const SceneWorldSnapshot& Other)
        : mReadOnlyWorld{ Other.mReadOnlyWorld }
        , mWorld{ Other.mWorld }
        , mAssetRegistry{ Other.mAssetRegistry }
        , mSceneName{ Other.mSceneName }
        , mSystemNames{ Other.mSystemNames }
        , mEntities{ Other.mEntities }
        , mRootIndices{ Other.mRootIndices }
        , mChildIndices{ Other.mChildIndices } {
    }

    SceneWorldSnapshot& SceneWorldSnapshot::operator=(const SceneWorldSnapshot& Other) {
        if (this == &Other) {
            return *this;
        }

        mReadOnlyWorld = Other.mReadOnlyWorld;
        mWorld = Other.mWorld;
        mAssetRegistry = Other.mAssetRegistry;
        mSceneName = Other.mSceneName;
        mSystemNames = Other.mSystemNames;
        mEntities = Other.mEntities;
        mRootIndices = Other.mRootIndices;
        mChildIndices = Other.mChildIndices;
        return *this;
    }

    SceneWorldSnapshot::SceneWorldSnapshot(SceneWorldSnapshot&& Other) noexcept
        : mReadOnlyWorld{ Other.mReadOnlyWorld }
        , mWorld{ Other.mWorld }
        , mAssetRegistry{ Other.mAssetRegistry }
        , mSceneName{ std::move(Other.mSceneName) }
        , mSystemNames{ std::move(Other.mSystemNames) }
        , mEntities{ std::move(Other.mEntities) }
        , mRootIndices{ std::move(Other.mRootIndices) }
        , mChildIndices{ std::move(Other.mChildIndices) } {
    }

    SceneWorldSnapshot& SceneWorldSnapshot::operator=(SceneWorldSnapshot&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mReadOnlyWorld = Other.mReadOnlyWorld;
        mWorld = Other.mWorld;
        mAssetRegistry = Other.mAssetRegistry;
        mSceneName = std::move(Other.mSceneName);
        mSystemNames = std::move(Other.mSystemNames);
        mEntities = std::move(Other.mEntities);
        mRootIndices = std::move(Other.mRootIndices);
        mChildIndices = std::move(Other.mChildIndices);
        return *this;
    }

    void SceneWorldSnapshot::BindReadOnlyWorld(const Arche::World::WorldReadOnlyView* ReadOnlyWorld) {
        mReadOnlyWorld = ReadOnlyWorld;
    }

    void SceneWorldSnapshot::BindWorld(Arche::World* World) {
        mWorld = World;
    }

    void SceneWorldSnapshot::Clear() {
        mSystemNames.clear();
        mEntities.clear();
        mRootIndices.clear();
        mChildIndices.clear();
    }

    void SceneWorldSnapshot::Reserve(std::size_t Capacity) {
        mEntities.reserve(Capacity);
        mRootIndices.reserve(Capacity);
        mChildIndices.reserve(Capacity);
    }

    void SceneWorldSnapshot::AddEntity(Arche::EntityID EntityId, Arche::EntityID ParentId) {
        SceneEntitySnapshot Snapshot{};
        Snapshot.mEntityId = EntityId;
        Snapshot.mParentId = ParentId;
        mEntities.push_back(Snapshot);
    }

    void SceneWorldSnapshot::AddSystemName(const std::string& SystemName) {
        mSystemNames.push_back(SystemName);
    }

    void SceneWorldSnapshot::SetSceneName(const std::string& SceneName) {
        mSceneName = SceneName;
    }

    void SceneWorldSnapshot::BuildHierarchy() {
        mRootIndices.clear();
        mChildIndices.clear();
        mChildIndices.resize(mEntities.size());

        std::unordered_map<Arche::EntityID, std::uint32_t> IndexByEntity{};
        IndexByEntity.reserve(mEntities.size());

        for (std::uint32_t EntityIndex{ 0 }; EntityIndex < static_cast<std::uint32_t>(mEntities.size()); ++EntityIndex) {
            IndexByEntity.emplace(mEntities[EntityIndex].mEntityId, EntityIndex);
        }

        for (std::uint32_t EntityIndex{ 0 }; EntityIndex < static_cast<std::uint32_t>(mEntities.size()); ++EntityIndex) {
            const Arche::EntityID ParentId{ mEntities[EntityIndex].mParentId };
            const std::unordered_map<Arche::EntityID, std::uint32_t>::iterator ParentIter{ IndexByEntity.find(ParentId) };

            if (ParentIter == IndexByEntity.end()) {
                mRootIndices.push_back(EntityIndex);
                continue;
            }

            mChildIndices[ParentIter->second].push_back(EntityIndex);
        }
    }

    const Arche::World::WorldReadOnlyView* SceneWorldSnapshot::GetReadOnlyWorld() const {
        return mReadOnlyWorld;
    }

    Arche::World* SceneWorldSnapshot::GetWorld() const {
        return mWorld;
    }

    void SceneWorldSnapshot::BindAssetRegistry(const AssetRegistry* AssetRegistryInstance) {
        mAssetRegistry = AssetRegistryInstance;
    }

    const AssetRegistry* SceneWorldSnapshot::GetAssetRegistry() const {
        return mAssetRegistry;
    }

    const std::string& SceneWorldSnapshot::GetSceneName() const {
        return mSceneName;
    }

    const std::vector<std::string>& SceneWorldSnapshot::GetSystemNames() const {
        return mSystemNames;
    }

    const std::vector<SceneWorldSnapshot::SceneEntitySnapshot>& SceneWorldSnapshot::GetEntities() const {
        return mEntities;
    }

    const std::vector<std::uint32_t>& SceneWorldSnapshot::GetRootIndices() const {
        return mRootIndices;
    }

    const std::vector<std::uint32_t>& SceneWorldSnapshot::GetChildIndices(std::uint32_t EntityIndex) const {
        static const std::vector<std::uint32_t> Empty{};

        if (EntityIndex >= mChildIndices.size()) {
            return Empty;
        }

        return mChildIndices[EntityIndex];
    }
}
