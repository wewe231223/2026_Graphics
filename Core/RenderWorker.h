#pragma once 
#include <thread>
#include <atomic>
#include <semaphore>
#include <barrier>
#include <array>
#include "Core/Task/TaskContainer.h"
#include "Core/Task/TaskInterface.h"
#include "Core/DX/GraphicsBuffer.h"
#include "Utility/CompileTimeConstants.h"

namespace Core {
    namespace Task {
        struct RenderFlowContext {
            TaskContainer<RenderTaskBase*, decltype(&GetTaskPSO)>       taskContainer;
            std::atomic<uint32_t>                                       nextTaskIdx{ 0 };

            // 임의 최대 워커 개수 ( 최대 256코어 ) 
            std::counting_semaphore<256>                                signalStart{ 0 };
            std::barrier<>                                              syncBarrier;

			uint32_t                                                    currentFrameIndex{ 0 };

            RenderFlowContext(int workerCount) : syncBarrier(workerCount + 1), taskContainer(&GetTaskPSO) {}
        };


        class RenderWorker : public RenderWorkerBase {
        public:
            RenderWorker(RenderFlowContext& context);

        public:
            void RequestStop();

            virtual ID3D12GraphicsCommandList* GetCommandList() const override;
            virtual DX::GraphicsBuffer& GetWorkerBuffer() override;

        private:
            void ExecuteInternal();

        private:
            RenderFlowContext&                                                          mContext;
            std::jthread                                                                mThread;

            std::array<DX::GraphicsBuffer, Constants::FrameCount<size_t>>               mWorkerBuffers{};

            ComPtr<ID3D12GraphicsCommandList>                                           mCmdList{ nullptr };
            std::array<ComPtr<ID3D12CommandAllocator>, Constants::FrameCount<size_t>>   mCommandAllocators{};
        };
    }
}