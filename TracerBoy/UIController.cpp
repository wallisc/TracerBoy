#include "pch.h"

UIController::UIController(HWND hwnd, ID3D12Device &device, ComPtr<IDXGISwapChain3> pSwapchain) :
	m_pSwapchain(pSwapchain)
{
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	//ImGui::StyleColorsDark();
	ImGui::StyleColorsClassic();


	DXGI_SWAP_CHAIN_DESC swapchainDesc;
	m_pSwapchain->GetDesc(&swapchainDesc);
	UINT bufferCount = swapchainDesc.BufferCount;
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = 1;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		VERIFY_HRESULT(device.CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_pImguiSRVDescriptorHeap)));
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		desc.NumDescriptors = bufferCount;
		VERIFY_HRESULT(device.CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_pImguiRTVDescriptorHeap)));
	}

	m_RTVs.resize(bufferCount);
	for (UINT i = 0; i < bufferCount; i++)
	{
		ComPtr<ID3D12Resource> pBackBuffer;
		m_pSwapchain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
		m_RTVs[i] = CD3DX12_CPU_DESCRIPTOR_HANDLE(
			m_pImguiRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			i,
			device.GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));

		device.CreateRenderTargetView(
			pBackBuffer.Get(),
			nullptr,
			m_RTVs[i]);
	}


	// Setup Platform/Renderer bindings
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(&device, bufferCount,
		swapchainDesc.BufferDesc.Format, m_pImguiSRVDescriptorHeap.Get(),
		m_pImguiSRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		m_pImguiSRVDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	// Load Fonts
	// - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
	// - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
	// - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
	// - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
	// - Read 'docs/FONTS.txt' for more instructions and details.
	// - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
	//io.Fonts->AddFontDefault();
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
	//ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
	//IM_ASSERT(font != NULL);
}

void UIController::Render(ID3D12GraphicsCommandList& commandList)
{
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::Begin("TracerBoy");
	ImGui::Text("%.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

	// General BeginCombo() API, you have full control over your selection data and display type.
	// (your selection data could be an index, a pointer to the object, an id for the object, a flag stored in the object itself, etc.)
	const char* OutputTypes[] = { "Lit", "Normals", "Albedo" };

	static int current = 0;
	ImGui::Combo("View Mode", &current, OutputTypes, IM_ARRAYSIZE(OutputTypes));
	ImGui::End();

	ImGui::Render();

	ComPtr<ID3D12Resource> pBackBuffer;
	UINT backBufferIndex = m_pSwapchain->GetCurrentBackBufferIndex();
	m_pSwapchain->GetBuffer(backBufferIndex, IID_PPV_ARGS(&pBackBuffer));

	ID3D12DescriptorHeap* pDescriptorHeaps[] = { m_pImguiSRVDescriptorHeap.Get() };
	commandList.SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);
	commandList.OMSetRenderTargets(1, &m_RTVs[backBufferIndex], FALSE, NULL);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), &commandList);
	D3D12_RESOURCE_BARRIER postImguiBarriers[] =
	{
		CD3DX12_RESOURCE_BARRIER::Transition(pBackBuffer.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,  D3D12_RESOURCE_STATE_PRESENT)
	};
	commandList.ResourceBarrier(ARRAYSIZE(postImguiBarriers), postImguiBarriers);
}

