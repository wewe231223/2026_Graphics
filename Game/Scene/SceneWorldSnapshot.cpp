#include "SceneWorldSnapshot.h"
#include <unordered_map>

namespace Game {
    SceneWorldSnapshot::SceneWorldSnapshot()
        : mReadOnlyWorld{},
        mEntities{},
        mRootIndices{},
        mChildIndices{} {
    }

    SceneWorldSnapshot::~SceneWorldSnapshot() {
    }

    SceneWorldSnapshot::SceneWorldSnapshot(const SceneWorldSnapshot& Other)
        : mReadOnlyWorld{ Other.mReadOnlyWorld },
        mEntities{ Other.mEntities },
        mRootIndices{ Other.mRootIndices },
        mChildIndices{ Other.mChildIndices } {
    }

    SceneWorldSnapshot& SceneWorldSnapshot::operator=(const SceneWorldSnapshot& Other) {
        if (this == &Other) {
            return *this;
        }

        mReadOnlyWorld = Other.mReadOnlyWorld;
        mEntities = Other.mEntities;
        mRootIndices = Other.mRootIndices;
        mChildIndices = Other.mChildIndices;
        return *this;
    }

    SceneWorldSnapshot::SceneWorldSnapshot(SceneWorldSnapshot&& Other) noexcept
        : mReadOnlyWorld{ Other.mReadOnlyWorld },
        mEntities{ std::move(Other.mEntities) },
        mRootIndices{ std::move(Other.mRootIndices) },
        mChildIndices{ std::move(Other.mChildIndices) } {
    }

    SceneWorldSnapshot& SceneWorldSnapshot::operator=(SceneWorldSnapshot&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mReadOnlyWorld = Other.mReadOnlyWorld;
        mEntities = std::move(Other.mEntities);
        mRootIndices = std::move(Other.mRootIndices);
        mChildIndices = std::move(Other.mChildIndices);
        return *this;
    }

    void SceneWorldSnapshot::BindReadOnlyWorld(const Arche::World::WorldReadOnlyView* ReadOnlyWorld) {
        mReadOnlyWorld = ReadOnlyWorld;
    }

    void SceneWorldSnapshot::Clear() {
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

    void SceneWorldSnapshot::BuildHierarchy() {
        mRootIndices.clear();
        mChildIndices.clear();
        mChildIndices.resize(mEntities.size());

        std::unordered_map<std::uint64_t, std::uint32_t> IndexByEntity{};
        IndexByEntity.reserve(mEntities.size());

        for (std::uint32_t EntityIndex{ 0 }; EntityIndex < static_cast<std::uint32_t>(mEntities.size()); ++EntityIndex) {
            IndexByEntity.emplace(ToEntityKey(mEntities[EntityIndex].mEntityId), EntityIndex);
        }

        for (std::uint32_t EntityIndex{ 0 }; EntityIndex < static_cast<std::uint32_t>(mEntities.size()); ++EntityIndex) {
            const Arche::EntityID ParentId{ mEntities[EntityIndex].mParentId };
            const std::unordered_map<std::uint64_t, std::uint32_t>::iterator ParentIter{ IndexByEntity.find(ToEntityKey(ParentId)) };

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

    std::uint64_t SceneWorldSnapshot::ToEntityKey(Arche::EntityID EntityId) const {
        return (static_cast<std::uint64_t>(EntityId.generation) << 32) | static_cast<std::uint64_t>(EntityId.index);
    }
}
