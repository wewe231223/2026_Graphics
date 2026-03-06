#pragma once 
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <vector>
#include <memory>
#include <string>
#include "Utility/DirectXInclude.h"
#include "Common.h"

namespace Widget {
	class ResourceBrowserWidget;

	class WidgetCore {
	public:
		WidgetCore() = default;
		~WidgetCore();

		WidgetCore(const WidgetCore&) = delete;
		WidgetCore& operator=(const WidgetCore&) = delete;

		WidgetCore(WidgetCore&&) noexcept = default;
		WidgetCore& operator=(WidgetCore&&) noexcept = default;

	public:
		void Initialize(HWND hWnd, ComPtr<ID3D12Device>& device, IDXGIAdapter1* adapter);
		void Render(ComPtr<ID3D12GraphicsCommandList>& commandList);
		void SetCurrentSceneName(const std::string& SceneName);

		template<std::derived_from<IWidget> T, typename... Args>
		void MakeWidget(Args&&... args) {
			mWidgets.emplace_back(std::make_unique<T>(std::forward<Args>(args)...));
		}

	private:
		void BuildWidgets();

	private:
		ComPtr<ID3D12DescriptorHeap> mSRVHeap{ nullptr };
		std::vector<std::unique_ptr<IWidget>> mWidgets{};
		ResourceBrowserWidget* mResourceBrowserWidget{ nullptr };
	};
}
