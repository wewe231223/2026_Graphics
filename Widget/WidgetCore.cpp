#include <filesystem>
#include "WidgetCore.h"
#include "../External/Include/ImGui/imgui.h"
#include "../External/Include/ImGui/imgui_impl_dx12.h"
#include "../External/Include/ImGui/imgui_impl_win32.h"
#include "../External/Include/ImGui/imgui_internal.h"
#include "Utility/ErrorHandler.h"
#include "Utility/CompileTimeConstants.h"

namespace fs = std::filesystem;


#include "Console.h"
#include "PerformanceProvider.h"
#include "PerformanceWidgets.h"
#include "ResourceBrowserWidget.h"

namespace Widget {

	WidgetCore::~WidgetCore() {
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}

	void WidgetCore::Initialize(HWND hWnd, ComPtr<ID3D12Device>& device, IDXGIAdapter1* adapter) {
		D3D12_DESCRIPTOR_HEAP_DESC desc{};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = 1;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ErrorHandler::report(FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&mSRVHeap))), "WidgetCore", "Failed To Make Widget DescriptorHeap", ErrorHandler::Level::Critical);

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;


		ImGui::StyleColorsDark();


		auto imWin32 = ImGui_ImplWin32_Init(hWnd);
		auto imDx12 = ImGui_ImplDX12_Init(
			device.Get(),
			Constants::FrameCount<int>,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			mSRVHeap.Get(),
			mSRVHeap->GetCPUDescriptorHandleForHeapStart(),
			mSRVHeap->GetGPUDescriptorHandleForHeapStart()
		);

		if (not imWin32 or not imDx12) {
			::abort();
		}


		if (fs::exists(Constants::FontPath)) {
			ImFontConfig fontConfig{};
			fontConfig.OversampleH = 3;
			fontConfig.OversampleV = 1;
			fontConfig.PixelSnapH = true;
			fontConfig.MergeMode = false;
			io.Fonts->AddFontFromFileTTF(Constants::FontPath, 24.f, &fontConfig, io.Fonts->GetGlyphRangesKorean());
			io.Fonts->Build();
		}

		PerformanceProvider::Get().Initialize(adapter);

		WidgetCore::BuildWidgets();
	}

	void WidgetCore::Render(ComPtr<ID3D12GraphicsCommandList>& commandList) {
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();


		for (const auto& widget : mWidgets) {
			widget->Render();
		}

		ImGui::Render();
		commandList->SetDescriptorHeaps(1, mSRVHeap.GetAddressOf());
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());

		ImGuiIO& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault(nullptr, (void*)commandList.Get());
		}
	}

	void WidgetCore::SetCurrentSceneName(const std::string& SceneName) {
		if (mResourceBrowserWidget == nullptr) {
			return;
		}

		mResourceBrowserWidget->SetCurrentSceneName(SceneName);
	}

	void WidgetCore::BuildWidgets() {
		MakeWidget<FrameTimeWidget>();
		MakeWidget<DistributionWidget>();
		MakeWidget<TimelineWidget>();
		MakeWidget<VramUsageWidget>();
		MakeWidget<ImGuiConsole>();
		MakeWidget<ResourceBrowserWidget>();

		if (mWidgets.empty() == false) {
			mResourceBrowserWidget = dynamic_cast<ResourceBrowserWidget*>(mWidgets.back().get());
		}
	}
}