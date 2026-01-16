#include "RenderWorker.h"
using namespace Core::Task;

RenderWorker::RenderWorker(RenderFlowContext& context) : mContext(context) {
	mThread = std::jthread([this](std::stop_token st) {
		while (true) {
		    mContext.signalStart.acquire();
		
		    if (st.stop_requested()) {
		        mContext.syncBarrier.arrive_and_drop(); 
		        return;
		    }
		
		    ExecuteInternal();
		
		    mContext.syncBarrier.arrive_and_wait();
		}
	});
}

void RenderWorker::RequestStop() {
	mThread.request_stop();
}

ID3D12GraphicsCommandList* Core::Task::RenderWorker::GetCommandList() const {
	return nullptr;
}

Core::DX::GraphicsBuffer& Core::Task::RenderWorker::GetWorkerBuffer() {
	return mWorkerBuffers[mContext.currentFrameIndex]; 
}

void RenderWorker::ExecuteInternal() {
	while (true) {
		uint32_t taskIdx = mContext.nextTaskIdx.fetch_add(1, std::memory_order_relaxed);
		if (taskIdx >= mContext.taskContainer.Size()) {
			break;
		}

		const auto& task = mContext.taskContainer [taskIdx];
		
		task->Process(*this); 
	}
}
