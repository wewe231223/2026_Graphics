#pragma once
#define WIN32_LEAN_AND_MEAN
#include <array>
#include <cstdint>
#include <Windows.h>
#include "Utility/DirectXInclude.h"
#include "Utility/CompileTimeConstants.h"

namespace Core {
    namespace DX {
        class FrameSync {
        public:
            FrameSync(ID3D12Device* device);
            ~FrameSync();

        public:
            void Sync(ID3D12CommandQueue* commandQueue);
            uint32_t GetCurrentIndex() const;

            void Flush(ID3D12CommandQueue* commandQueue);
        private:
            ComPtr<ID3D12Fence>                                     mFence{ nullptr };
            HANDLE                                                  mSyncEvent{ nullptr };

            std::array<uint64_t, Constants::FrameCount<size_t>>     mFenceValues{};
            uint32_t                                                mFrameIndex{};
            uint64_t                                                mValueCounter{};
        };
    }
}