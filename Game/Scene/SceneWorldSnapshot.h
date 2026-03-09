#pragma once

#include <cstdint>
#include <vector>
#include "Arche/Common.h"
#include "Arche/World.h"

namespace Game {
    class SceneWorldSnapshot final {
    public:
        struct SceneEntitySnapshot final {
            Arche::EntityID mEntityId{ Arche::NullEntityID };
            Arche::EntityID mParentId{ Arche::NullEntityID };
        };

    public:
        SceneWorldSnapshot();
        ~SceneWorldSnapshot();

        SceneWorldSnapshot(const SceneWorldSnapshot& Other);
        SceneWorldSnapshot& operator=(const SceneWorldSnapshot& Other);

        SceneWorldSnapshot(SceneWorldSnapshot&& Other) noexcept;
        SceneWorldSnapshot& operator=(SceneWorldSnapshot&& Other) noexcept;

    public:
        void BindReadOnlyWorld(const Arche::World::WorldReadOnlyView* ReadOnlyWorld);

        void Clear();
        void Reserve(std::size_t Capacity);
        void AddEntity(Arche::EntityID EntityId, Arche::EntityID ParentId);
        void BuildHierarchy();

        const Arche::World::WorldReadOnlyView* GetReadOnlyWorld() const;

        const std::vector<SceneEntitySnapshot>& GetEntities() const;
        const std::vector<std::uint32_t>& GetRootIndices() const;
        const std::vector<std::uint32_t>& GetChildIndices(std::uint32_t EntityIndex) const;

    private:
        std::uint64_t ToEntityKey(Arche::EntityID EntityId) const;

    private:
        const Arche::World::WorldReadOnlyView* mReadOnlyWorld{};
        std::vector<SceneEntitySnapshot> mEntities{};
        std::vector<std::uint32_t> mRootIndices{};
        std::vector<std::vector<std::uint32_t>> mChildIndices{};
    };
}
