#pragma once
#include <cstdint>

namespace Core {
    namespace DX {
        namespace CopyQueueId {
            static constexpr std::uint64_t DrawCallBegin{ 0 };
            static constexpr std::uint64_t DrawCallEnd{ 10 };
            static constexpr std::uint64_t Model{ 11 };
        }
    }
}
