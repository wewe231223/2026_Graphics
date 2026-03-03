#pragma once
#include <cstdint>
#include "Core/DX/DesciptorHeap.h"
#include "Game/Base/RenderFrameData.h"
#include "Utility/DirectXInclude.h"

namespace Core {
	namespace DX {
		class DrawCallDispatcher {
		public:
			DrawCallDispatcher();
			~DrawCallDispatcher();
			DrawCallDispatcher(const DrawCallDispatcher& Other) = delete;
			DrawCallDispatcher& operator=(const DrawCallDispatcher& Other) = delete;
			DrawCallDispatcher(DrawCallDispatcher&& Other) = delete;
			DrawCallDispatcher& operator=(DrawCallDispatcher&& Other) = delete;

		public:
			void DrawForward(ID3D12GraphicsCommandList* CommandList, Game::RFD::RenderFrameData& Data, DescriptorHandle FrameGlobalsSrvHandle, DescriptorHandle ModelContextSrvHandle, DescriptorHandle DrawRecordSrvHandle);
		};
	}
}
