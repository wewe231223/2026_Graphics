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

            ID3D12Resource* GetDefault() const; 
            D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() const;
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

        class MeshBuffer : public GraphicsResource {
        public:
            MeshBuffer() = default;
            MeshBuffer(ID3D12Device* device, UINT vertexCount, UINT vertexStride, UINT indexCount, DXGI_FORMAT indexFormat = DXGI_FORMAT_R32_UINT);

            virtual ~MeshBuffer() = default;

            // 이동 생성자 및 대입 연산자 (리소스 소유권 관리)
            MeshBuffer(MeshBuffer&& other) noexcept;
            MeshBuffer& operator=(MeshBuffer&& other) noexcept;

            // 복사 방지
            MeshBuffer(const MeshBuffer&) = delete;
            MeshBuffer& operator=(const MeshBuffer&) = delete;

        public:
            void UpdateVertices(const void* data, UINT64 size);

            template<typename T>
            void UpdateVertices(const std::vector<T>& data) {
                UpdateVertices(data.data(), data.size() * sizeof(T));
            }

            void UpdateIndices(const void* data, UINT64 size);

            template<typename T>
            void UpdateIndices(const std::vector<T>& data) {
                UpdateIndices(data.data(), data.size() * sizeof(T));
            }

            const D3D12_VERTEX_BUFFER_VIEW& GetVBV() const { return mVBV; }
            const D3D12_INDEX_BUFFER_VIEW& GetIBV() const { return mIBV; }
            UINT GetIndexCount() const { return mIndexCount; }

        private:
            static UINT64 CalculateTotalSize(UINT vertexCount, UINT vertexStride, UINT indexCount, DXGI_FORMAT indexFormat = DXGI_FORMAT_R32_UINT);
        private:
            UINT64 mVertexDataSize = 0;
            UINT64 mIndexDataSize = 0;
            UINT64 mIndexOffset = 0; // 버퍼 내에서 인덱스 데이터 시작 오프셋

            UINT mIndexCount = 0;

            D3D12_VERTEX_BUFFER_VIEW mVBV{};
            D3D12_INDEX_BUFFER_VIEW mIBV{};
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
            // 기본 할당 크기는 128MB
            GraphicsBuffer(ID3D12Device* device, UINT64 capacity = 128 * 1024 * 1024);
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