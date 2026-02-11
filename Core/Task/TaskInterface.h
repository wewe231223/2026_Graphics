#pragma once 
#include "Utility/DirectXInclude.h"
#include "Core/DX/GraphicsBuffer.h"

namespace Core {
    namespace Task {
        class RenderWorkerBase {
        public:
            virtual ~RenderWorkerBase() = default;

			virtual ID3D12GraphicsCommandList* GetCommandList() const PURE;
            virtual Core::DX::GraphicsBuffer& GetWorkerBuffer() PURE;
        };

        class RenderTaskBase {
        public:
            virtual ~RenderTaskBase() = default;
            virtual ID3D12PipelineState* GetPSO() const PURE;
            virtual void Process(RenderWorkerBase& worker) PURE;
        };

        inline ID3D12PipelineState* GetTaskPSO(RenderTaskBase* task) {
            return task->GetPSO();
        }
    }
}
