#include <algorithm>
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
		if (!mIsInitialized) {
			return;
		}

		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}

	void WidgetCore::Initialize(HWND hWnd, ID3D12Device* Device, IDXGIAdapter1* Adapter) {
		D3D12_DESCRIPTOR_HEAP_DESC desc{};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
#pragma region TemporaryShadowMapPreview
		desc.NumDescriptors = 1 + ShadowMapPreviewCapacity;
#pragma endregion
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ErrorHandler::report(FAILED(Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&mSRVHeap))), "WidgetCore", "Failed To Make Widget DescriptorHeap", ErrorHandler::Level::Critical);
#pragma region TemporaryShadowMapPreview
		mDevice = Device;
		mDescriptorIncrementSize = Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		const D3D12_CPU_DESCRIPTOR_HANDLE HeapCpuStartHandle{ mSRVHeap->GetCPUDescriptorHandleForHeapStart() };
		const D3D12_GPU_DESCRIPTOR_HANDLE HeapGpuStartHandle{ mSRVHeap->GetGPUDescriptorHandleForHeapStart() };
		for (std::uint32_t ShadowMapIndex{ 0 }; ShadowMapIndex < ShadowMapPreviewCapacity; ShadowMapIndex += 1) {
			mShadowMapSrvCpuHandles[ShadowMapIndex] = HeapCpuStartHandle;
			mShadowMapSrvGpuHandles[ShadowMapIndex] = HeapGpuStartHandle;
			mShadowMapSrvCpuHandles[ShadowMapIndex].ptr += static_cast<SIZE_T>(mDescriptorIncrementSize) * static_cast<SIZE_T>(ShadowMapIndex + 1);
			mShadowMapSrvGpuHandles[ShadowMapIndex].ptr += static_cast<UINT64>(mDescriptorIncrementSize) * static_cast<UINT64>(ShadowMapIndex + 1);
		}
#pragma endregion

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
		mIsInitialized = true;
	}

	void WidgetCore::Render(ComPtr<ID3D12GraphicsCommandList>& commandList) {
		if (!mIsInitialized) {
			return;
		}

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

		RenderRenderSettings();

#pragma region TemporaryShadowMapPreview
		if (mShadowMapCount > 0) {
			ImGui::Begin("Shadow Map");
			const float ShadowMapDisplaySize{ static_cast<float>(std::min<std::uint32_t>(mShadowMapSize, 512u)) };
			ImGui::Text("Resolution: %u", mShadowMapSize);
			if (ImGui::BeginTable("ShadowMapTable", 2, ImGuiTableFlags_SizingFixedFit)) {
				for (std::uint32_t ShadowMapIndex{ 0 }; ShadowMapIndex < mShadowMapCount; ShadowMapIndex += 1) {
					if (mShadowMapResources[ShadowMapIndex] == nullptr || mShadowMapSrvGpuHandles[ShadowMapIndex].ptr == 0) {
						continue;
					}

					ImGui::TableNextColumn();
					ImGui::Text("Cascade %u", ShadowMapIndex);
					ImGui::Image((ImTextureID)mShadowMapSrvGpuHandles[ShadowMapIndex].ptr, ImVec2{ ShadowMapDisplaySize, ShadowMapDisplaySize });
				}

				ImGui::EndTable();
			}

			ImGui::End();
		}
#pragma endregion

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

#pragma region TemporaryShadowMapPreview
	void WidgetCore::SetShadowMapTextures(const std::array<ID3D12Resource*, ShadowMapPreviewCapacity>& Resources, std::uint32_t ShadowMapCount, std::uint32_t ShadowMapSize) {
		if (mDevice == nullptr) {
			return;
		}

		mShadowMapCount = std::min<std::uint32_t>(ShadowMapCount, ShadowMapPreviewCapacity);
		mShadowMapSize = ShadowMapSize;
		for (std::uint32_t ShadowMapIndex{ 0 }; ShadowMapIndex < ShadowMapPreviewCapacity; ShadowMapIndex += 1) {
			ID3D12Resource* ShadowMapResource{ ShadowMapIndex < mShadowMapCount ? Resources[ShadowMapIndex] : nullptr };
			if (mShadowMapResources[ShadowMapIndex] == ShadowMapResource) {
				continue;
			}

			mShadowMapResources[ShadowMapIndex] = ShadowMapResource;
			if (ShadowMapResource == nullptr || mShadowMapSrvCpuHandles[ShadowMapIndex].ptr == 0) {
				continue;
			}

			D3D12_SHADER_RESOURCE_VIEW_DESC ShadowMapSrvDescription{};
			ShadowMapSrvDescription.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
			ShadowMapSrvDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			ShadowMapSrvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			ShadowMapSrvDescription.Texture2D.MipLevels = 1;
			mDevice->CreateShaderResourceView(ShadowMapResource, &ShadowMapSrvDescription, mShadowMapSrvCpuHandles[ShadowMapIndex]);
		}
	}
#pragma endregion

	void WidgetCore::SetSceneWorldSnapshot(const Game::SceneWorldSnapshot* Snapshot) {
		mSceneWorldSnapshot = Snapshot;
	}

	bool WidgetCore::IsPostProcessEnabled() const {
		return mIsPostProcessEnabled;
	}


	void WidgetCore::BuildWidgets() {
		MakeWidget<FrameTimeWidget>();
		MakeWidget<DistributionWidget>();
		MakeWidget<TimelineWidget>();
		MakeWidget<VramUsageWidget>();
		MakeWidget<ImGuiConsole>();
		MakeWidget<SceneHierarchyWidget>();
	}

	void WidgetCore::RenderRenderSettings() {
		if (!ImGui::Begin("Render Settings")) {
			ImGui::End();
			return;
		}

		ImGui::Checkbox("Post Process", &mIsPostProcessEnabled);

		ImGui::End();
	}
}
