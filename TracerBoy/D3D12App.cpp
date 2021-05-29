#include "pch.h"

const DXGI_FORMAT cBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

D3D12App::D3D12App(HWND hwnd, LPSTR pCommandLine) : 
	m_mouseX(0), 
	m_mouseY(0),
	m_SignalValue(1),
	m_bIsRecording(false),
	m_bRenderUI(true)
{
	ZeroMemory(m_inputArray, sizeof(m_inputArray));
#if 0
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
	swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	VERIFY_HRESULT(pDxgiFactory2->CreateSwapChainForHwnd(m_pCommandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr, &pSwapChain));

	VERIFY_HRESULT(pSwapChain.As(&m_pSwapChain));
	std::string commandLine(pCommandLine);

	m_pUIController = std::unique_ptr<UIController>(new UIController(hwnd, *m_pDevice.Get(), m_pSwapChain));
	m_pTracerBoy = std::unique_ptr<TracerBoy>(new TracerBoy(m_pCommandQueue.Get()));

	std::vector<ComPtr<ID3D12Resource>> scratchResources;
	CommandListAllocatorPair commandListAllocatorPair;
	AcquireCommandListAllocatorPair(commandListAllocatorPair);
	ID3D12GraphicsCommandList& commandList = *commandListAllocatorPair.first.Get();
	m_pTracerBoy->LoadScene(commandList, commandLine, scratchResources);
	VERIFY_HRESULT(commandListAllocatorPair.first->Close());
	ExecuteAndFreeCommandListAllocatorPair(commandListAllocatorPair);

	for (auto &pReadbackStatBuffer : m_pReadbackStatBuffers)
	{
		const D3D12_HEAP_PROPERTIES readbackHeapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
		D3D12_RESOURCE_DESC readbackBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT32) * 256);

		VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
			&readbackHeapDesc,
			D3D12_HEAP_FLAG_NONE,
			&readbackBufferDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(pReadbackStatBuffer.ReleaseAndGetAddressOf())));
	}

	WaitForGPUIdle();
}

UINT64 D3D12App::SignalFence()
{
	UINT64 signalledValue = m_SignalValue;
	VERIFY_HRESULT(m_pCommandQueue->Signal(m_pFence.Get(), m_SignalValue++));
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

	TracerBoy::CameraSettings cameraSettings = {};
	cameraSettings.m_movementSpeed = m_pUIController->GetCameraSpeed();
	cameraSettings.m_ignoreMouse = !m_MouseMovementEnabled;

	TracerBoy::ReadbackStats TracerStats;
	{
		TracerBoy::ReadbackStats *pTracerStats;
		m_pReadbackStatBuffers[backBufferIndex]->Map(0, nullptr, (void**)&pTracerStats);
		memcpy(&TracerStats, pTracerStats, sizeof(TracerStats));
		m_pReadbackStatBuffers[backBufferIndex]->Unmap(0, nullptr);
	}

	if (m_bIsRecording)
	{
		bool bFrameStart = m_SamplesRendered == 0;
		timeSinceLastUpdate = bFrameStart ? 1000.0f / m_pUIController->GetCaptureFramesPerSecond() : 0.0f;
	}

	m_pTracerBoy->Update(m_mouseX, m_mouseY, m_inputArray, timeSinceLastUpdate, cameraSettings);

	ComPtr<ID3D12Resource> pBackBuffer;
	m_pSwapChain->GetBuffer(backBufferIndex, IID_PPV_ARGS(&pBackBuffer));

	CommandListAllocatorPair commandListAllocatorPair;
	AcquireCommandListAllocatorPair(commandListAllocatorPair);
	ID3D12GraphicsCommandList& commandList = *commandListAllocatorPair.first.Get();

	m_pTracerBoy->Render(commandList, pBackBuffer.Get(), m_pReadbackStatBuffers[backBufferIndex].Get(), m_pUIController->GetOutputSettings());

	static bool bConverged = false;
	if (m_pTracerBoy->GetNumberOfSamplesSinceLastInvalidate() == 1)
	{
		m_TimeSinceLastInvalidate = std::chrono::steady_clock::now();
		bConverged = false;
	}

	if (m_bRenderUI)
	{
		UIController::PerFrameStats stats;

		stats.NumberOfWavesExecuted = TracerStats.ActiveWaves;
		stats.WavesWithLivePixels = TracerStats.WavesWithActivePixels;
		if (stats.WavesWithLivePixels < 20 && !bConverged)
		{
			m_TimeSinceConvergence = std::chrono::steady_clock::now();
			bConverged = true;
		}
		stats.ElapsedTimeSinceLastInvalidate = std::chrono::duration_cast<std::chrono::milliseconds>(
			(bConverged ? m_TimeSinceConvergence : std::chrono::steady_clock::now()) 
			- m_TimeSinceLastInvalidate).count() / 1000.0f;

		m_pUIController->Render(commandList, stats);
	}

	commandList.Close();

	ID3D12CommandList* CommandLists[] = { &commandList };
	ExecuteAndFreeCommandListAllocatorPair(commandListAllocatorPair);

	OnSampleSubmit(*pBackBuffer.Get());
	m_pSwapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
}

void D3D12App::OnSampleSubmit(ID3D12Resource &backBuffer)
{
	if (m_bIsRecording)
	{
		m_SamplesRendered++;
		if (m_SamplesRendered == m_pUIController->GetCaptureSamplesPerFrame())
		{
			std::wstring filename = L"render/frame" + std::to_wstring(m_FramesRendered) + L".jpg";
			DirectX::ScratchImage scratchImage;
			DirectX::CaptureTexture(m_pCommandQueue.Get(), &backBuffer, false, scratchImage, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PRESENT);
			DirectX::SaveToWICFile(*scratchImage.GetImage(0, 0, 0), DirectX::WIC_FLAGS_FORCE_SRGB, GUID_ContainerFormatPng, filename.c_str(), nullptr);

			m_FramesRendered++;
			m_SamplesRendered = 0;
		}

		if (m_FramesRendered == m_pUIController->GetCaptureLengthInSeconds() * m_pUIController->GetCaptureFramesPerSecond())
		{
			StopRecording();
		}
	}
}
