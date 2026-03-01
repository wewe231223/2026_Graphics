#pragma once 
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <vector>
#include <memory>
#include "Utility/DirectXInclude.h"
#include "Common.h"

class WidgetCore {
public:
	WidgetCore() = default; 
	~WidgetCore();

	WidgetCore(const WidgetCore&) = delete;
	WidgetCore& operator=(const WidgetCore&) = delete;

	WidgetCore(WidgetCore&&) noexcept = default;
	WidgetCore& operator=(WidgetCore&&) noexcept = default;

public:
	void Initialize(HWND hWnd, ComPtr<ID3D12Device>& device);
	void Render(ComPtr<ID3D12GraphicsCommandList>& commandList);

	template<std::derived_from<IWidget> T, typename... Args>
	void MakeWidget(Args&&... args) {
		mWidgets.emplace_back(std::make_unique<T>(std::forward<Args>(args)...));
	}

private:
	ComPtr<ID3D12DescriptorHeap> mSRVHeap{ nullptr };
	std::vector<std::unique_ptr<IWidget>> mWidgets{};
};