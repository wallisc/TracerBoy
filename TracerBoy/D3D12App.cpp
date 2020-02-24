#include "pch.h"

const DXGI_FORMAT cBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

D3D12App::D3D12App(HWND hwnd, LPSTR pCommandLine) : 
	m_mouseX(0), 
	m_mouseY(0),
	m_SignalValue(1)
{
	ZeroMemory(m_inputArray, sizeof(m_inputArray));
#ifdef DEBUG
	ComPtr<ID3D12Debug> debugController;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
	{
		debugController->EnableDebugLayer();
	}
#endif

	VERIFY_HRESULT(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_pDevice)));

	D3D12_COMMAND_QUEUE_DESC createCommandQueueDesc = {};
	createCommandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	createCommandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	VERIFY_HRESULT(m_pDevice->CreateCommandQueue(&createCommandQueueDesc, IID_PPV_ARGS(&m_pCommandQueue)));

	VERIFY_HRESULT(m_pDevice->CreateFence(m_SignalValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_pFence.ReleaseAndGetAddressOf())));

	ComPtr<IDXGIFactory2> pDxgiFactory2;
	VERIFY_HRESULT(CreateDXGIFactory2(0, IID_PPV_ARGS(&pDxgiFactory2)));

	RECT clientRect;
	if (!GetClientRect(hwnd, &clientRect)) HANDLE_FAILURE();

	ComPtr<IDXGISwapChain1> pSwapChain;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = cNumBackBuffers;
	swapChainDesc.Width = clientRect.right - clientRect.left;
	swapChainDesc.Height = clientRect.bottom - clientRect.top;
	swapChainDesc.Format = cBackBufferFormat;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	swapChainDesc.SampleDesc.Count = 1;
	VERIFY_HRESULT(pDxgiFactory2->CreateSwapChainForHwnd(m_pCommandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr, &pSwapChain));

	VERIFY_HRESULT(pSwapChain.As(&m_pSwapChain));
	std::string commandLine(pCommandLine);

	InitImigui(hwnd);


	m_pTracerBoy = std::unique_ptr<TracerBoy>(new TracerBoy(m_pCommandQueue.Get()));

	std::vector<ComPtr<ID3D12Resource>> scratchResources;
	CommandListAllocatorPair commandListAllocatorPair;
	AcquireCommandListAllocatorPair(commandListAllocatorPair);
	ID3D12GraphicsCommandList& commandList = *commandListAllocatorPair.first.Get();
	m_pTracerBoy->LoadScene(commandList, commandLine, scratchResources);
	VERIFY_HRESULT(commandListAllocatorPair.first->Close());
	ExecuteAndFreeCommandListAllocatorPair(commandListAllocatorPair);

	WaitForGPUIdle();
}

UINT64 D3D12App::SignalFence()
{
	UINT64 signalledValue = m_SignalValue;
	m_pCommandQueue->Signal(m_pFence.Get(), m_SignalValue++);
	return signalledValue;
}

void D3D12App::WaitForGPUIdle()
{
	WaitForFenceValue(SignalFence());
}

void D3D12App::WaitForFenceValue(UINT fenceValue)
{
	HANDLE waitEvent = CreateEvent(nullptr, false, false, nullptr);
	m_pFence->SetEventOnCompletion(fenceValue, waitEvent);
	WaitForSingleObject(waitEvent, INFINITE);
	CloseHandle(waitEvent);
}


void D3D12App::AcquireCommandListAllocatorPair(CommandListAllocatorPair& pair)
{
	if (FreedCommandListAllocatorPairs.size() && m_pFence->GetCompletedValue() >= FreedCommandListAllocatorPairs.back().second)
	{
		pair = FreedCommandListAllocatorPairs.back().first;
		FreedCommandListAllocatorPairs.pop_back();

		VERIFY_HRESULT(pair.second->Reset());
		VERIFY_HRESULT(pair.first->Reset(pair.second.Get(), nullptr));
	}
	else
	{
		VERIFY_HRESULT(m_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(pair.second.ReleaseAndGetAddressOf())));
		VERIFY_HRESULT(m_pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pair.second.Get(), nullptr, IID_PPV_ARGS(pair.first.ReleaseAndGetAddressOf())))
	}
}

UINT D3D12App::ExecuteAndFreeCommandListAllocatorPair(CommandListAllocatorPair& pair)
{
	ID3D12CommandList* pCommandLists[] = { pair.first.Get() };
	m_pCommandQueue->ExecuteCommandLists(ARRAYSIZE(pCommandLists), pCommandLists);

	UINT64 signalledValue = SignalFence();
	FreedCommandListAllocatorPairs.push_front(std::pair<CommandListAllocatorPair, UINT64>(pair, signalledValue));

	return signalledValue;
}

void D3D12App::InitImigui(HWND hwnd)
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

	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = 1;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		VERIFY_HRESULT(m_pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_pImguiSRVDescriptorHeap)));
	}
	
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		desc.NumDescriptors = cNumBackBuffers;
		VERIFY_HRESULT(m_pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_pImguiRTVDescriptorHeap)));
	}


	// Setup Platform/Renderer bindings
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(m_pDevice.Get(), cNumBackBuffers,
		cBackBufferFormat, m_pImguiSRVDescriptorHeap.Get(),
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


void D3D12App::UpdateMousePosition(int x, int y)
{
	m_mouseX = x;
	m_mouseY = y;
}

void D3D12App::Render()
{
	UINT backBufferIndex = m_pSwapChain->GetCurrentBackBufferIndex();
	WaitForFenceValue(m_FrameFence[backBufferIndex]);

	const bool bFirstUpdateCall = m_LastUpdateTime == std::chrono::steady_clock::time_point();
	std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();
	float timeSinceLastUpdate = bFirstUpdateCall ? 0.0f : 
		std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - m_LastUpdateTime).count();
	m_LastUpdateTime = std::chrono::steady_clock::now();

	m_pTracerBoy->Update(m_mouseX, m_mouseY, m_inputArray, timeSinceLastUpdate);

	ComPtr<ID3D12Resource> pBackBuffer;
	m_pSwapChain->GetBuffer(backBufferIndex, IID_PPV_ARGS(&pBackBuffer));
	if (m_RTV[backBufferIndex].ptr == 0)
	{
		m_RTV[backBufferIndex] = CD3DX12_CPU_DESCRIPTOR_HANDLE(
			m_pImguiRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			backBufferIndex,
			m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));

		m_pDevice->CreateRenderTargetView(
			pBackBuffer.Get(),
			nullptr,
			m_RTV[backBufferIndex]);
	}

	CommandListAllocatorPair commandListAllocatorPair;
	AcquireCommandListAllocatorPair(commandListAllocatorPair);
	ID3D12GraphicsCommandList& commandList = *commandListAllocatorPair.first.Get();
	m_pTracerBoy->Render(commandList, pBackBuffer.Get());

	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::Begin("TracerBoy");                          // Create a window called "Hello, world!" and append into it.
	//ImGui::ShowDemoWindow();
	ImGui::Text("%.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	ImGui::End();

	ImGui::Render();

	ID3D12DescriptorHeap *pDescriptorHeaps[] = { m_pImguiSRVDescriptorHeap.Get() };
	commandList.SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);
	commandList.OMSetRenderTargets(1, &m_RTV[backBufferIndex], FALSE, NULL);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), &commandList);
	D3D12_RESOURCE_BARRIER postImguiBarriers[] =
	{
		CD3DX12_RESOURCE_BARRIER::Transition(pBackBuffer.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,  D3D12_RESOURCE_STATE_PRESENT)
	};
	commandList.ResourceBarrier(ARRAYSIZE(postImguiBarriers), postImguiBarriers);
	commandList.Close();

	ID3D12CommandList* CommandLists[] = { &commandList };
	ExecuteAndFreeCommandListAllocatorPair(commandListAllocatorPair);

	m_pSwapChain->Present(0, 0);
}