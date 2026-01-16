#pragma once 
#include <unordered_map>
#include <vector>
#include <typeindex>
#include <span>
#include <string>
#include <algorithm>
#include <stdexcept>
#include "Utility/DirectXInclude.h" 
#include "Utility/ErrorHandler.h"

// TODO 
/*
 VRAM(비디오 메모리) 낭비:
일반적인 더블 버퍼링은 Upload Heap(시스템 메모리)만 프레임 수만큼 늘리고, Default Heap(VRAM)은 하나만 유지합니다.
질문하신 방식대로 클래스를 통째로 복제하면 VRAM에 있는 버퍼도 똑같이 복제됩니다. 버퍼 크기가 작다면(몇 MB 단위) 상관없지만, 크기가 크다면 비효율적입니다.
 */

namespace Core {
    namespace DX {
        class GraphicsResource {
        public:
            GraphicsResource();
            GraphicsResource(ID3D12Device* device, UINT64 capacity, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);
            virtual ~GraphicsResource();

			GraphicsResource(GraphicsResource const&) = delete;
			GraphicsResource& operator=(GraphicsResource const&) = delete;

            GraphicsResource(GraphicsResource&& other) noexcept;
            GraphicsResource& operator=(GraphicsResource&& other) noexcept;

        public:
            virtual void CopyToGPU(ID3D12GraphicsCommandList* cmdList);

            void Transition(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES newState);

            void Write(const void* data, UINT64 size, UINT64 offset = 0);

            // 게터
            ID3D12Resource* GetDefault() const { return mDefaultHeap.Get(); }
            D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() const { return mDefaultHeap->GetGPUVirtualAddress(); }
            D3D12_RESOURCE_STATES GetCurrentState() const;

        protected:
            void Unmap();

        protected:
            ComPtr<ID3D12Resource> mDefaultHeap{ nullptr };
            ComPtr<ID3D12Resource> mUploadHeap{ nullptr };
            D3D12_RESOURCE_STATES mDefaultHeapState{ D3D12_RESOURCE_STATE_COMMON };
            uint8_t* mMappedPtr{ nullptr };
            UINT64 mCapacity{};
        };

        class GraphicsBuffer : public GraphicsResource {
            using ResourceKey = uint64_t;

            struct SubResourceInfo {
                UINT64  baseOffset;
                UINT    stride;
                UINT    maxElementCount;
                UINT    currentElementCursor;
            };

            struct TypeEntry {
                ResourceKey         key;
                UINT64              stride;
#if defined(DEBUG) || defined(_DEBUG)
                std::string         debugName;
#endif
            };

        public:
            GraphicsBuffer();
            // 기본 할당 크기는 256MB
            GraphicsBuffer(ID3D12Device* device, UINT64 capacity = 256 * 1024 * 1024);
            ~GraphicsBuffer();

            GraphicsBuffer(const GraphicsBuffer&) = delete;
            GraphicsBuffer& operator=(const GraphicsBuffer&) = delete;

            GraphicsBuffer(GraphicsBuffer&& other) noexcept;
            GraphicsBuffer& operator=(GraphicsBuffer&& other) noexcept;

        public:
            template<typename T>
            void RegisterType(const std::string& name = "") {
                if (mIsFinalized) {
                    return;
                }

#if defined(DEBUG) || defined(_DEBUG)
                mRegisteredTypes.push_back({ GenerateKey<T>(name), sizeof(T), std::string(typeid(T).name()) + " : " + name});
#else
				mRegisteredTypes.push_back({ GenerateKey<T>(name), sizeof(T) });
#endif 
            }

            template<typename T>
            void Push(const T& data, const std::string& name = "") {
                auto& info = GetSubResourceInfo(GenerateKey<T>(name));
                if (info.currentElementCursor < info.maxElementCount) {
                    T* dest = reinterpret_cast<T*>(mMappedPtr + info.baseOffset);
                    dest[info.currentElementCursor++] = data;
                }
            }

            template<typename T>
            void Push(std::span<const T> dataSpan, const std::string& name = "") {
                auto& info = GetSubResourceInfo(GenerateKey<T>(name));
                const uint64_t availableCount = static_cast<uint64_t>(info.maxElementCount - info.currentElementCursor);
                const uint64_t pushCount = (std::min)(static_cast<uint64_t>(dataSpan.size()), availableCount);

                if (pushCount == 0) {
                    return;
                }

                T* dest = reinterpret_cast<T*>(mMappedPtr + info.baseOffset);
                memcpy(&dest[info.currentElementCursor], dataSpan.data(), static_cast<size_t>(pushCount * sizeof(T)));
                info.currentElementCursor += static_cast<UINT>(pushCount);
            }

            template<typename T>
            UINT GetCurrentCount(const std::string& name = "") const {
                return GetSubResourceInfo(GenerateKey<T>(name)).currentElementCursor;
            }

            template<typename T>
            D3D12_GPU_VIRTUAL_ADDRESS GetBaseGPUAddress(const std::string& name = "") {
                if (mDefaultHeap == nullptr) {
                    return 0;
                }
                return mDefaultHeap->GetGPUVirtualAddress() + GetSubResourceInfo(GenerateKey<T>(name)).baseOffset;
            }

            template<typename T>
            UINT GetStride(const std::string& name = "") {
                return GetSubResourceInfo(GenerateKey<T>(name)).stride;
            }


            void Finalize();
            void CopyDirtyRegionsToGPU(ID3D12GraphicsCommandList* cmdList);  
            void Reset();

#if defined(DEBUG) || defined(_DEBUG)
            std::string GetBufferStatusString() const;
#endif 
        private:
            template<typename T>
            static ResourceKey GenerateKey(const std::string& name) {
                size_t h1 = std::type_index(typeid(T)).hash_code();
                size_t h2 = std::hash<std::string>{}(name);
                return static_cast<ResourceKey>(h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2)));
            }

            SubResourceInfo& GetSubResourceInfo(ResourceKey key);
            const SubResourceInfo& GetSubResourceInfo(ResourceKey key) const;

        private:
            std::vector<TypeEntry>                              mRegisteredTypes;
            std::unordered_map<ResourceKey, SubResourceInfo>    mSubResources;


            bool                                                mIsFinalized{ false };
        };
    }
}