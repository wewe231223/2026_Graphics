#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include "Common.h"
#include "ArcheType.h"
#include "Utility/ComponentRestraint.h"

namespace Arche {
    class World {
    public:
        class WorldReadOnlyView final {
        public:
            WorldReadOnlyView();
            ~WorldReadOnlyView();

            WorldReadOnlyView(const WorldReadOnlyView& Other);
            WorldReadOnlyView& operator=(const WorldReadOnlyView& Other);

            WorldReadOnlyView(WorldReadOnlyView&& Other) noexcept;
            WorldReadOnlyView& operator=(WorldReadOnlyView&& Other) noexcept;

        public:
            void BindWorld(const World* TargetWorld);
            std::uint64_t GetStructureVersion() const;

            template <TrivialComponent T>
            const T* GetComponent(EntityID Id) const;

        private:
            const World* mWorld{};
        };

        template <TrivialComponent... Ts>
        class QueryView;

    public:
        World();
        ~World();

        World(const World& Other) = delete;
        World& operator=(const World& Other) = delete;

        World(World&& Other) noexcept = delete;
        World& operator=(World&& Other) noexcept = delete;

    public:
        template <TrivialComponent... Ts>
        EntityID CreateEntity(Ts... Args);

        template <TrivialComponent... Ts>
        EntityID CreateEntity();

        template <TrivialComponent T>
        void AddComponent(EntityID Id, T Component);

        void DestroyEntity(EntityID Id);

        template <TrivialComponent T>
        T* GetComponent(EntityID Id);

        template <TrivialComponent T>
        const T* GetComponent(EntityID Id) const;

        template <TrivialComponent T, typename Func>
        bool ReadComponent(EntityID Id, Func&& Reader) const;

        template <TrivialComponent T, typename Func>
        bool WriteComponent(EntityID Id, Func&& Writer);

        template <TrivialComponent... Ts>
        std::vector<Archetype*>* GetTargetArchetypes();

        template <TrivialComponent... Ts, typename Func>
        void ForEach(Func&& FuncObject);

        template <TrivialComponent... Ts>
        QueryView<Ts...> Query();

        template <TrivialComponent... Ts>
        void DeferCreateEntity(Ts... Args);

        template <TrivialComponent T>
        void DeferAddComponent(EntityID Id, T Component);

        void DeferDestroyEntity(EntityID Id);
        void FlushDeferredStructuralChanges();

        std::uint64_t GetStructureVersion() const;
        const WorldReadOnlyView& GetReadOnlyView() const;

    private:
        struct QueryCache {
            std::vector<TypeID> mSignature{};
            std::vector<Archetype*> mArchetypes{};
        };

        Archetype* GetOrCreateArchetype(std::span<const TypeID> SortedIDs, std::span<const size_t> Sizes, std::span<const size_t> Aligns);
        void GetArchetypeInfo(Archetype* Arch, std::vector<TypeID>& OutIds, std::vector<size_t>& OutSizes, std::vector<size_t>& OutAligns);
        void TouchStructureVersion();

        template <TrivialComponent... Ts>
        EntityID CreateEntityInternal(Ts... Args);

        template <TrivialComponent T>
        void AddComponentInternal(EntityID Id, T Component);

        void DestroyEntityInternal(EntityID Id);

        template <TrivialComponent T>
        T* GetComponentUnsafe(EntityID Id);

        template <TrivialComponent T>
        const T* GetComponentUnsafe(EntityID Id) const;

    private:
        std::vector<EntityRecord> mEntityTable{};
        std::vector<std::uint32_t> mFreeIndices{};
        std::vector<std::unique_ptr<Archetype>> mArcheTypes{};

        std::deque<QueryCache> mQueryCaches{};
        std::deque<std::function<void(World&)>> mDeferredStructuralCommands{};

        mutable std::shared_mutex mWorldRwLock{};
        std::mutex mDeferredQueueLock{};

        std::atomic_uint64_t mStructureVersion{};
        WorldReadOnlyView mReadOnlyView{};
    };

    template <TrivialComponent... Ts>
    class World::QueryView {
    public:
        struct Iterator {
        public:
            using PointerTuple = std::tuple<Ts*...>;
            using ReferenceTuple = std::tuple<Ts&...>;

        public:
            Iterator(std::vector<Archetype*>::const_iterator It, std::vector<Archetype*>::const_iterator End);
            ~Iterator() = default;

            Iterator(const Iterator& Other) = default;
            Iterator& operator=(const Iterator& Other) = default;

            Iterator(Iterator&& Other) noexcept = default;
            Iterator& operator=(Iterator&& Other) noexcept = default;

        public:
            Iterator& operator++();
            bool operator==(const Iterator& Other) const;
            bool operator!=(const Iterator& Other) const;
            ReferenceTuple operator*() const;

        private:
            void AdvanceToValidChunk();
            void UpdatePointers();

        private:
            std::vector<Archetype*>::const_iterator mArchIt{};
            std::vector<Archetype*>::const_iterator mArchEnd{};
            std::vector<Chunk*>::const_iterator mChunkIt{};
            std::uint32_t mEntityIndex{};
            PointerTuple mCurrentTargetArray{};
        };

    public:
        QueryView(const std::vector<Archetype*>* Archetypes);
        ~QueryView() = default;

        QueryView(const QueryView& Other) = default;
        QueryView& operator=(const QueryView& Other) = default;

        QueryView(QueryView&& Other) noexcept = default;
        QueryView& operator=(QueryView&& Other) noexcept = default;

    public:
        Iterator begin();
        Iterator end();

    private:
        const std::vector<Archetype*>* mArcheTypes{};
    };
}

#include "World.inl"
