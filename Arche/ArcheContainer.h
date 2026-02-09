#pragma once
#include <vector>
#include <memory>
#include <algorithm>
#include <tuple>
#include <deque>
#include <array>
#include <span>
#include "Common.h"
#include "Archetype.h"

namespace Arche {
    class ArcheContainer {
    private:
        std::vector<EntityRecord> mEntityTable{};
        std::vector<std::uint32_t> mFreeIndices{};
        std::vector<std::unique_ptr<Archetype>> mArcheTypes{};
        struct QueryCache { std::vector<TypeID> signature{}; std::vector<Archetype*> archetypes{}; };
        std::vector<QueryCache> mQueryCaches{};

    private:
        // Archetype 내부 Layout 출력 
        Archetype* GetOrCreateArchetype(std::span<const TypeID> sortedIDs, std::span<const size_t> sizes, std::span<const size_t> aligns);        
        void GetArchetypeInfo(Archetype* arch, std::vector<TypeID>& outIds, std::vector<size_t>& outSizes, std::vector<size_t>& outAligns);

    public:
        ArcheContainer() = default;
        ~ArcheContainer() = default;

		ArcheContainer(const ArcheContainer&) = delete;
		ArcheContainer& operator=(const ArcheContainer&) = delete;

		ArcheContainer(ArcheContainer&&) noexcept = default;
		ArcheContainer& operator=(ArcheContainer&&) noexcept = default;

    public:
        template <typename... Ts>
        EntityID CreateEntity(Ts... args) {
            std::uint32_t idx;

            if (!mFreeIndices.empty()) { // 빈 자리가 있다면 
                idx = mFreeIndices.back();
                mFreeIndices.pop_back();
            }
            else { // 없으면 새로 추가 
                idx = static_cast<std::uint32_t>(mEntityTable.size());
                mEntityTable.resize(idx + 1);
                mEntityTable[idx].generation = 0;
            }

            EntityRecord& record{ mEntityTable[idx] };// 새로운 Entity 의 record -> id 를 통해 archetype 종류를 매핑
            record.active = true;
            EntityID id{ idx, record.generation }; // 리턴값

            // ArcheType의 위치를 찾기 위한 작업 
            constexpr size_t Count = sizeof...(Ts);

            struct ComponentMeta { TypeID id; size_t size; size_t align; const void* ptr; };
            std::array<ComponentMeta, Count> meta = { {
                { TypeInfo<Ts>::ID, sizeof(Ts), alignof(Ts), &args }...
            } };

            std::sort(meta.begin(), meta.end(), [](const auto& a, const auto& b) { return a.id < b.id; });

            std::array<TypeID, Count> ids;
            std::array<size_t, Count> sizes;
            std::array<size_t, Count> aligns;
            std::vector<std::pair<TypeID, const void*>> ptrs;
            ptrs.reserve(Count);

            for (size_t i = 0; i < Count; ++i) {
                ids[i] = meta[i].id;
                sizes[i] = meta[i].size;
                aligns[i] = meta[i].align;
                ptrs.emplace_back(meta[i].id, meta[i].ptr);
            }

			// 새로운 Entity 가 들어가야 할 Archetype 획득
            Archetype* arch{ GetOrCreateArchetype(ids, sizes, aligns) };

            // record 에 찾은 ArcheType 기록
            arch->PushEntity(id, ptrs);
            record.archetype = arch;
            record.chunkIndex = static_cast<std::uint32_t>(arch->GetChunks().size() - 1);
            record.entityIndex = arch->GetChunks().back()->count - 1;

            return id;
        }

        template <typename... Ts>
        EntityID CreateEntity() {
            return CreateEntity(Ts{}...);
        }

        template <typename T>
        void AddComponent(EntityID id, T component) {
            if (id.index >= mEntityTable.size()) { // 에러 id 
                return;
            }

            EntityRecord& record = mEntityTable[id.index];
			if (!record.active or record.generation != id.generation) { // invalid id
                return;
            }

            Archetype* oldArch{ record.archetype };
			if (oldArch->HasType(TypeInfo<T>::ID)) { // 추가하려는 타입이 이미 컴포넌트에 있다
                return;
            }

            std::vector<TypeID> ids; 
            std::vector<size_t> sizes; 
            std::vector<size_t> aligns;
            GetArchetypeInfo(oldArch, ids, sizes, aligns); // 기존 정보 추출

            // 새로운 타입 추가
			ids.emplace_back(TypeInfo<T>::ID); 
            sizes.emplace_back(sizeof(T)); 
            aligns.emplace_back(alignof(T));

            // 새로운 조합의 Entity 가 들어갈 ArcheType 을 찾는 과정
            struct Meta { TypeID id; size_t s; size_t a; };
            std::vector<Meta> combined;
            for (size_t i = 0; i < ids.size(); ++i) {
                combined.emplace_back(ids[i], sizes[i], aligns[i]);
            }
            std::sort(combined.begin(), combined.end(), [](const auto& a, const auto& b) { return a.id < b.id; });

            ids.clear(); 
            sizes.clear(); 
            aligns.clear();

            for (const auto& c : combined) { 
                ids.emplace_back(c.id); 
                sizes.emplace_back(c.s); 
                aligns.emplace_back(c.a); 
            }

			// 새로운 ArcheType 획득
            Archetype* newArch{ GetOrCreateArchetype(ids, sizes, aligns) };

            // 기존 데이터 이사갈 준비 
            std::vector<std::pair<TypeID, const void*>> moveData;
            Chunk* oldChunk = oldArch->GetChunks()[record.chunkIndex];

            // 기존 데이터 복사
            for (TypeID tId : oldArch->GetSignature()) {
                moveData.emplace_back(tId, oldArch->GetComponentPtr(oldChunk, record.entityIndex, tId));
            }
            // 새로운 데이터 추가 
            moveData.emplace_back(TypeInfo<T>::ID, &component);

            // 이사
            newArch->PushEntity(id, moveData);

			// 이사 후 기록 갱신
            EntityID movedID = oldArch->PopEntity(record.chunkIndex, record.entityIndex);
            if (movedID != id) {
                EntityRecord& movedRecord = mEntityTable[movedID.index];
                movedRecord.chunkIndex = record.chunkIndex; movedRecord.entityIndex = record.entityIndex;
            }
            record.archetype = newArch;
            record.chunkIndex = static_cast<std::uint32_t>(newArch->GetChunks().size() - 1);
            record.entityIndex = newArch->GetChunks().back()->count - 1;
        }

        template <typename... Ts>
        std::vector<Archetype*>* GetTargetArchetypes() {
			// 쿼리 캐시에 있는지 확인하기 위한 시그니쳐 표준화 
            std::vector<TypeID> querySig = { TypeInfo<Ts>::ID... }; 
            std::sort(querySig.begin(), querySig.end());

            // 검색 
            for (auto& cache : mQueryCaches) {
                if (cache.signature == querySig) { // 있으면? 
                    return &cache.archetypes;
                }
            }

            // 없으면? -> 새로운 캐시 생성 
            QueryCache newCache;
            newCache.signature = querySig;
            for (auto& arch : mArcheTypes) {
                const auto& as = arch->GetSignature();
				// 새로운 캐시에 해당하는 아키타입을 새로운 캐시 기록에 추가 
                if (std::includes(as.begin(), as.end(), querySig.begin(), querySig.end())) {
                    newCache.archetypes.emplace_back(arch.get());
                }
            }
            mQueryCaches.emplace_back(std::move(newCache));

            return &mQueryCaches.back().archetypes;
        }

        // inline ForEach -> 가장 빠른 방법 ( callable 지원 ) 
        template <typename... Ts, typename Func>
        void ForEach(Func&& func) {
            for (Archetype* arch : *ArcheContainer::GetTargetArchetypes()) {
                for (Chunk* chunk : arch->GetChunks()) {
                    std::tuple<Ts*...> pointers = { static_cast<Ts*>(arch->GetBaseComponentArray(chunk, TypeInfo<Ts>::ID))... };
                    size_t count{ chunk->count };
                    for (size_t i = 0; i < count; ++i) {
                        std::apply([&](auto*... ptrs) { func(ptrs[i]...); }, pointers);
                    }
                }
            }
        }

        void DestroyEntity(EntityID id);

        template <typename T>
        T* GetComponent(EntityID id) {
			if (id.index >= mEntityTable.size()) { // 에러 id
                return nullptr;
            }

            EntityRecord& r{ mEntityTable[id.index] };

			if (!r.active or r.generation != id.generation) { // invalid id
                return nullptr;
            }

            Chunk* c{ r.archetype->GetChunks()[r.chunkIndex] }; // 찾아서 캐스팅 후 리턴 
            return static_cast<T*>(r.archetype->GetComponentPtr(c, r.entityIndex, TypeInfo<T>::ID));
        }

        // ArcheType 을 활용하기 위한 view 를 제공하기 위한 객체
        // 속도를 위해 인라인으로 처리한다. 
        template <typename... Ts>
        class QueryView {
        public:
            // range 순회를 위한 반복자 
            struct Iterator {
                using pointer_tuple = std::tuple<Ts*...>;
                using reference_tuple = std::tuple<Ts&...>;

            private:
                std::vector<Archetype*>::iterator mArchIt{};
                std::vector<Archetype*>::iterator mArchEnd{};
                std::vector<Chunk*>::const_iterator mChunkIt{};
                std::uint32_t mEntityIndex{};
                pointer_tuple mCurrentTargetArray{};

            public:
                Iterator(std::vector<Archetype*>::iterator it, std::vector<Archetype*>::iterator end)
                    : mArchIt(it), mArchEnd(end), mEntityIndex(0) {

                    if (mArchIt != mArchEnd) {
                        mChunkIt = (*mArchIt)->GetChunks().begin();
                    }

                    AdvanceToValidChunk();
                }

                Iterator(const Iterator&) = default;
				Iterator& operator=(const Iterator&) = default;

				Iterator(Iterator&&) noexcept = default;
				Iterator& operator=(Iterator&&) noexcept = default;

                Iterator& operator++() {
                    mEntityIndex++;
                    // 현재 청크를 벗어나면 다음 청크로 이동
                    if (mEntityIndex >= (*mChunkIt)->count) {
                        mEntityIndex = 0;
                        ++mChunkIt; // 청크 전진 
                        AdvanceToValidChunk(); // 청크 유효성 검사 
                    }
                    return *this;
                }

                bool operator!=(const Iterator& other) const {
                    return mArchIt != other.mArchIt;
                }

                reference_tuple operator*() const { // 전체 튜플을 돌며 인덱싱 이후 다시 참조 튜플로 반환
                    return std::apply([this](auto*... ptrs) {
                        return std::forward_as_tuple(ptrs[mEntityIndex]...);
                        }, mCurrentTargetArray);
                }

            private:
                // 현재 ArcheType 에서 다음 유효한 청크로 이동
                void AdvanceToValidChunk() {
                    while (mArchIt != mArchEnd) {
                        const auto& chunks = (*mArchIt)->GetChunks();

                        // 현재 ArcheType의 청크들을 순회 
                        while (mChunkIt != chunks.end()) { // 비어있는 청크 등을 위해 while 
                            if ((*mChunkIt)->count > 0) { // 다음 청크가 내용물이 있다면 
                                UpdatePointers();
                                return;
                            }
                            ++mChunkIt; // 없다면 청크 전진 
                        }

                        // 현재 ArcheType의 모든 청크를 다 봤으므로 다음 ArcheType으로 이동
                        ++mArchIt;
                        mEntityIndex = 0;

                        // 다음 ArcheType이 있다면, 그 ArcheType의 첫 청크로 반복자 갱신
                        if (mArchIt != mArchEnd) {
                            mChunkIt = (*mArchIt)->GetChunks().begin();
                        }
                    }
                }

                void UpdatePointers() {
                    Archetype* arch{ *mArchIt };
                    Chunk* chunk{ *mChunkIt };

                    // 청크를 기반으로 각 컴포넌트 타입 복원 
                    mCurrentTargetArray = std::make_tuple(
                        static_cast<Ts*>(arch->GetBaseComponentArray(chunk, TypeInfo<Ts>::ID))...
                    );
                }
            };

        private:
            std::vector<Archetype*>* mArcheTypes{};

		public:
            QueryView(std::vector<Archetype*>* archs) : mArcheTypes(archs) {}
			~QueryView() = default;

			QueryView(const QueryView&) = default;
			QueryView& operator=(const QueryView&) = default;

			QueryView(QueryView&&) noexcept = default;
			QueryView& operator=(QueryView&&) noexcept = default;

        public:
            Iterator begin() { 
                return Iterator(mArcheTypes->begin(), mArcheTypes->end()); 
            }

            Iterator end() { 
                return Iterator(mArcheTypes->end(), mArcheTypes->end()); 
            }
        };


        template <typename... Ts>
        QueryView<Ts...> Query() {
            // 캐시된 ArcheTpye 리스트를 가져와서 View를 생성해 반환
            return QueryView<Ts...>(GetTargetArchetypes<Ts...>());
        }

    };
}