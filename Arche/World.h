#pragma once
#include <algorithm>
#include <array>
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

namespace Arche {
    template <typename T>
    concept TrivialComponent = std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>;

    class World {
    public:
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

    private:
        struct QueryCache {
            std::vector<TypeID> mSignature{};
            std::vector<Archetype*> mArchetypes{};
        };

        Archetype* GetOrCreateArchetype(std::span<const TypeID> SortedIDs, std::span<const size_t> Sizes, std::span<const size_t> Aligns);

        void GetArchetypeInfo(Archetype* Arch, std::vector<TypeID>& OutIds, std::vector<size_t>& OutSizes, std::vector<size_t>& OutAligns);

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
    };

    template <TrivialComponent... Ts>
    class World::QueryView {
    public:
        struct Iterator {
        public:
            using PointerTuple = std::tuple<Ts*...>;
            using ReferenceTuple = std::tuple<Ts&...>;

        public:
            Iterator(std::vector<Archetype*>::iterator It, std::vector<Archetype*>::iterator End);
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
            std::vector<Archetype*>::iterator mArchIt{};
            std::vector<Archetype*>::iterator mArchEnd{};
            std::vector<Chunk*>::const_iterator mChunkIt{};
            std::uint32_t mEntityIndex{};
            PointerTuple mCurrentTargetArray{};
        };

    public:
        QueryView(std::vector<Archetype*> Archs);
        ~QueryView() = default;

        QueryView(const QueryView& Other) = default;
        QueryView& operator=(const QueryView& Other) = default;

        QueryView(QueryView&& Other) noexcept = default;
        QueryView& operator=(QueryView&& Other) noexcept = default;

    public:
        Iterator begin();
        Iterator end();

    private:
        std::vector<Archetype*> mArcheTypes{};
    };
}

#include "World.inl"
