#include <filesystem>
#include "WidgetCore.h"
#include "../External/Include/ImGui/imgui.h"
#include "../External/Include/ImGui/imgui_impl_dx12.h"
#include "../External/Include/ImGui/imgui_impl_win32.h"
#include "../External/Include/ImGui/imgui_internal.h"
#include "Utility/ErrorHandler.h"
#include "Utility/CompileTimeConstants.h"
#include "Game/Base/Input.h"

namespace fs = std::filesystem;


#include "Console.h"
#include "PerformanceProvider.h"
#include "PerformanceWidgets.h"
#include "SceneHierarchyWidget.h"

namespace Widget {

	WidgetCore::~WidgetCore() {
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}

	void WidgetCore::Initialize(HWND hWnd, ID3D12Device* Device, IDXGIAdapter1* Adapter) {
		D3D12_DESCRIPTOR_HEAP_DESC desc{};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = 1;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ErrorHandler::report(FAILED(Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&mSRVHeap))), "WidgetCore", "Failed To Make Widget DescriptorHeap", ErrorHandler::Level::Critical);

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;


		ImGui::StyleColorsDark();
		ImGuiStyle& Style{ ImGui::GetStyle() };
		Style.WindowMinSize = ImVec2(320.0f, 180.0f);

		auto imWin32 = ImGui_ImplWin32_Init(hWnd);
		auto imDx12 = ImGui_ImplDX12_Init(
			Device,
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

		PerformanceProvider::Get().Initialize(Adapter);

		WidgetCore::BuildWidgets();
	}

	void WidgetCore::Render(ComPtr<ID3D12GraphicsCommandList>& commandList) {
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		const ImGuiViewport* MainViewport{ ImGui::GetMainViewport() };
		ImGui::SetNextWindowPos(MainViewport->WorkPos);
		ImGui::SetNextWindowSize(MainViewport->WorkSize);
		ImGui::SetNextWindowViewport(MainViewport->ID);

		constexpr ImGuiWindowFlags DockWindowFlags{ ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking };
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		const bool IsDockWindowOpen{ ImGui::Begin("MainDockSpace", nullptr, DockWindowFlags) };
		ImGui::PopStyleVar();

		if (IsDockWindowOpen) {
			ImGui::DockSpace(ImGui::GetID("MainDockSpaceNode"), ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
		}
		ImGui::End();

		for (const auto& widget : mWidgets) {
			widget->Render(mSceneWorldSnapshot);
		}

		const bool IsAnyImGuiWindowFocused{ ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow) };
		Globals::Input::Get().SetImGuiInputBlocked(IsAnyImGuiWindowFocused);

		ImGui::Render();
		commandList->SetDescriptorHeaps(1, mSRVHeap.GetAddressOf());
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());

		ImGuiIO& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault(nullptr, (void*)commandList.Get());
		}
	}

	void WidgetCore::SetSceneWorldSnapshot(const Game::SceneWorldSnapshot* Snapshot) {
	mSceneWorldSnapshot = Snapshot;
}

	void WidgetCore::BuildWidgets() {
		MakeWidget<FrameTimeWidget>();
		MakeWidget<DistributionWidget>();
		MakeWidget<TimelineWidget>();
		MakeWidget<VramUsageWidget>();
		MakeWidget<ImGuiConsole>();
		MakeWidget<SceneHierarchyWidget>();
	}
}