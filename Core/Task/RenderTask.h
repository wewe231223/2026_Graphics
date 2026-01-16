#pragma once 
#include "Core/Task/TaskInterface.h"

namespace Core {
	namespace Task {
		// 기본 워커 버퍼 초기화 작업. 이 작업으로 초기화된 버퍼는 사용 불가. 상속하여 버퍼 레이아웃을 넣어주거나, 버퍼를 사용하지 말 것. 
		class WorkerInitTask : public RenderTaskBase {
		public:
			virtual ID3D12PipelineState* GetPSO() const override {
				return 0;
			}

			virtual void Process(RenderWorkerBase& worker) override {
				ID3D12GraphicsCommandList* cmdList = worker.GetCommandList();
				ID3D12Device* device;
				cmdList->GetDevice(IID_PPV_ARGS(&device));
				worker.GetWorkerBuffer() = DX::GraphicsBuffer(device);
			}
		};
	}
}