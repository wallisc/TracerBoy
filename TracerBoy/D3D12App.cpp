#include "pch.h"

const DXGI_FORMAT cBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
D3D12App::D3D12App(DeviceWrapper &deviceWrapper, LPCSTR pCommandLine) : 
	m_deviceWrapper(deviceWrapper),
	m_mouseX(0), 
	m_mouseY(0),
	m_SignalValue(1),
	m_bIsRecording(false),
	m_bRenderUI(true),
	bIsSceneLoadFinished(false)
{
	ZeroMemory(m_inputArray, sizeof(m_inputArray));

	m_pDevice = &deviceWrapper.GetDevice();
	m_pCommandQueue = &deviceWrapper.GetPresentQueue();

	VERIFY_HRESULT(m_pDevice->CreateFence(m_SignalValue, D3D12_FENCE_FLAG_NONE, IID_GRAPHICS_PPV_ARGS(m_pFence.ReleaseAndGetAddressOf())));

#if ENABLE_UI
	HWND hwnd = {};
	IDXGISwapChain3* pSwapchain = nullptr;
	if (!deviceWrapper.GetWin32ExtensionData(&hwnd, (void **)(&pSwapchain)))
	{
		// Can't use UI because can't query Win32 windowing information
		HANDLE_FAILURE();
	}
	
	m_pUIController = std::unique_ptr<UIController>(new UIController(hwnd, *m_pDevice.Get(), *pSwapchain));
#endif

	std::string commandLine(pCommandLine);
	InitializeTracerBoy(commandLine);

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
			IID_GRAPHICS_PPV_ARGS(pReadbackStatBuffer.ReleaseAndGetAddressOf())));
	}
}

void D3D12App::InitializeTracerBoy(const std::string& commandLine)
{
	m_pTracerBoy = std::unique_ptr<TracerBoy>(new TracerBoy(m_pCommandQueue.Get()));
	bool bLoadSceneAsync = true;
	if (bLoadSceneAsync)
	{
		m_asyncLoadSceneThread = std::thread([this, commandLine]
			{
				CoInitialize(nullptr);
				SetThreadDescription(GetCurrentThread(), L"LoadSceneThread");
				this->LoadScene(commandLine);
			});
		m_asyncLoadSceneThread.detach();
	}
	else
	{
		LoadScene(commandLine);
	}
}

void D3D12App::LoadScene(const std::string& commandLine)
{
	std::vector<ComPtr<ID3D12Resource>> scratchResources;
	CommandListAllocatorPair commandListAllocatorPair;
	AcquireCommandListAllocatorPair(commandListAllocatorPair);
	ID3D12GraphicsCommandList& commandList = *commandListAllocatorPair.first.Get();
	m_pTracerBoy->LoadScene(commandList, commandLine, scratchResources, m_sceneLoadStatus, m_sceneLoadStatusUpdateLock);
	VERIFY_HRESULT(commandListAllocatorPair.first->Close());

	ExecuteAndFreeCommandListAllocatorPair(commandListAllocatorPair);
	WaitForGPUIdle();
	bIsSceneLoadFinished = true;

	ClearSelection();
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
	const std::lock_guard<std::mutex> lock(CommandListAllocatorPairMutex);

	if (FreedCommandListAllocatorPairs.size() && m_pFence->GetCompletedValue() >= FreedCommandListAllocatorPairs.back().second)
	{
		pair = FreedCommandListAllocatorPairs.back().first;
		FreedCommandListAllocatorPairs.pop_back();

		VERIFY_HRESULT(pair.second->Reset());
		VERIFY_HRESULT(pair.first->Reset(pair.second.Get(), nullptr));
	}
	else
	{
		VERIFY_HRESULT(m_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_GRAPHICS_PPV_ARGS(pair.second.ReleaseAndGetAddressOf())));
		VERIFY_HRESULT(m_pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pair.second.Get(), nullptr, IID_GRAPHICS_PPV_ARGS(pair.first.ReleaseAndGetAddressOf())))
	}
}

UINT D3D12App::ExecuteAndFreeCommandListAllocatorPair(CommandListAllocatorPair& pair)
{
	std::lock_guard<std::mutex> lock(CommandListAllocatorPairMutex);

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

void D3D12App::NotifyLeftMouseClick()
{
	m_pTracerBoy->SelectPixel(m_mouseX, m_mouseY);
	m_FramesSincePixelSelection = 0;
	m_bValidPixelSelection = true;
	ClearSelection();
}


void D3D12App::Render()
{
	UINT backBufferIndex = m_deviceWrapper.GetBackBufferIndex();
	WaitForFenceValue(m_FrameFence[backBufferIndex]);
	
#if ENABLE_UI
	if (m_pUIController->HasSceneChangeRequest())
	{
		WaitForGPUIdle();
		m_pTracerBoy = nullptr;
		bIsSceneLoadFinished = false;

		InitializeTracerBoy(m_pUIController->GetRequestedSceneName());
	}
#endif

	ComPtr<ID3D12Resource> pBackBuffer = &m_deviceWrapper.GetBackBuffer(backBufferIndex);

	CommandListAllocatorPair commandListAllocatorPair;
	AcquireCommandListAllocatorPair(commandListAllocatorPair);
	ID3D12GraphicsCommandList& commandList = *commandListAllocatorPair.first.Get();

	if (bIsSceneLoadFinished)
	{
		const bool bFirstUpdateCall = m_LastUpdateTime == std::chrono::steady_clock::time_point();
		std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();
		float timeSinceLastUpdate = bFirstUpdateCall ? 0.0f :
			std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - m_LastUpdateTime).count();
		m_LastUpdateTime = std::chrono::steady_clock::now();



		TracerBoy::CameraSettings cameraSettings = {};
#if ENABLE_UI
		cameraSettings.m_movementSpeed = m_pUIController->GetCameraSpeed();
#else
		cameraSettings.m_movementSpeed = DEFAULT_CAMERA_SPEED;
#endif
		cameraSettings.m_ignoreMouse = !m_MouseMovementEnabled;

		TracerBoy::ReadbackStats TracerStats;
		{
			TracerBoy::ReadbackStats* pTracerStats;
			m_pReadbackStatBuffers[backBufferIndex]->Map(0, nullptr, (void**)&pTracerStats);
			memcpy(&TracerStats, pTracerStats, sizeof(TracerStats));
			m_pReadbackStatBuffers[backBufferIndex]->Unmap(0, nullptr);
		}

#if ENABLE_UI
		if (m_bIsRecording)
		{
			bool bFrameStart = m_SamplesRendered == 0;
			timeSinceLastUpdate = bFrameStart ? 1000.0f / m_pUIController->GetCaptureFramesPerSecond() : 0.0f;
		}
#endif

		ControllerState controllerState = {};
		m_deviceWrapper.GetControllerState(controllerState);
		m_pTracerBoy->Update(m_mouseX, m_mouseY, m_inputArray, timeSinceLastUpdate, controllerState, cameraSettings);


#if ENABLE_UI
		const auto& outputSettings = m_pUIController->GetOutputSettings();
#else
		auto outputSettings = TracerBoy::GetDefaultOutputSettings();
#endif

		bool bFlushGPU = m_pTracerBoy->RequiresGPUFlush(outputSettings);
#if ENABLE_UI
		bool bHasRecompileRequest = m_pUIController->HasRecompileRequest();
		bFlushGPU = bFlushGPU || bHasRecompileRequest;
#endif
		if (bFlushGPU)
		{
			WaitForGPUIdle();
		}

#if ENABLE_UI
		if (bHasRecompileRequest)
		{
			m_pTracerBoy->RecompileShaders();
			m_pUIController->NotifyRecompileComplete();
		}
#endif

		m_pTracerBoy->Render(commandList, pBackBuffer.Get(), m_pReadbackStatBuffers[backBufferIndex].Get(), outputSettings);

		uint TotalPixels = pBackBuffer->GetDesc().Width * pBackBuffer->GetDesc().Height;
		{
			float colorInterp = (float)TracerStats.ActivePixels / (float)TotalPixels;

			auto Lerp = [](const pbrt::math::vec3f& a, const pbrt::math::vec3f& b, float interpValue) -> pbrt::math::vec3f
			{
				return a * (1.0 - interpValue) + b * interpValue;
			};

			const pbrt::math::vec3f red = pbrt::math::vec3f(1.0, 0.0, 0.0);
			const pbrt::math::vec3f green = pbrt::math::vec3f(0.0, 1.0, 0.0);
			const pbrt::math::vec3f blue = pbrt::math::vec3f(0.0, 0.0, 1.0);

			// Gradually change the color from red -> blue -> green
			pbrt::math::vec3f color = (colorInterp > 0.5f) ?
				Lerp(blue, red, (colorInterp - 0.5f) * 2.0f) :
				Lerp(green, blue, colorInterp * 2.0f);

			// Visual tweak: Keep at least one of the 3 colors channels at 1.0 so that the lights stay bright
			float colorPadding = std::min(std::min(1.0f - color.x, 1.0f - color.y), 1.0f - color.z);
			color = color + pbrt::math::vec3f(colorPadding);

			m_razerChromaManager.UpdateLighting(color.x, color.y, color.z);
		}

		static bool bConverged = false;
		if (m_pTracerBoy->GetNumberOfSamplesSinceLastInvalidate() == 1)
		{
			m_TimeSinceLastInvalidate = std::chrono::steady_clock::now();
			bConverged = false;
		}

#if ENABLE_UI
		bool bPixelSelectionQueryFinished = m_FramesSincePixelSelection == cNumBackBuffers;
		if (bPixelSelectionQueryFinished)
		{
			m_pixelSelection.DistanceFromCamera = TracerStats.SelectedPixelDistance;
			m_pSelectedMaterial = m_pTracerBoy->GetMaterial(TracerStats.SelectedMaterialID, &m_pixelSelection.MaterialName);
			if (m_pSelectedMaterial)
			{
				m_pixelSelection.Material = *m_pSelectedMaterial;
			}
			
		}
#endif
		m_FramesSincePixelSelection++;

#if ENABLE_UI
		if (m_bRenderUI)
		{
			PIXScopedEvent(&commandList, PIX_COLOR_DEFAULT, L"UI");
			UIController::PerFrameStats stats;

			stats.NumberOfWavesExecuted = TracerStats.ActiveWaves;
			stats.NumberOfPixelsActive = TracerStats.ActivePixels;
			stats.NumberOfTotalPixels = TotalPixels;
			if (stats.NumberOfWavesExecuted < 20 && !bConverged)
			{
				m_TimeSinceConvergence = std::chrono::steady_clock::now();
				bConverged = true;
			}
			stats.ElapsedTimeSinceLastInvalidate = std::chrono::duration_cast<std::chrono::milliseconds>(
				(bConverged ? m_TimeSinceConvergence : std::chrono::steady_clock::now())
				- m_TimeSinceLastInvalidate).count() / 1000.0f;
			
			bool bValidSelectedMaterial = m_pSelectedMaterial;
			m_pUIController->Render(commandList, stats, m_bValidPixelSelection ? &m_pixelSelection : nullptr);

			bool bMaterialHasBeenModified = bValidSelectedMaterial && memcmp(&m_pixelSelection.Material, m_pSelectedMaterial, sizeof(m_pixelSelection.Material)) != 0;
			if (bMaterialHasBeenModified)
			{
				m_pTracerBoy->SetMaterial(TracerStats.SelectedMaterialID, m_pixelSelection.Material);
			}
		}
#endif
	}
	else
	{
		SceneLoadStatus sceneLoadStatus;

		// Make a copy of the scene load status so that we're not holding the lock any longer than neccessary
		{
			const std::lock_guard<std::mutex> lock(m_sceneLoadStatusUpdateLock);
			sceneLoadStatus = m_sceneLoadStatus;
		}
#if ENABLE_UI
		m_pUIController->RenderLoadingScreen(commandList, sceneLoadStatus);
#endif
	}

	commandList.Close();

	ID3D12CommandList* CommandLists[] = { &commandList };
	ExecuteAndFreeCommandListAllocatorPair(commandListAllocatorPair);

	OnSampleSubmit(*pBackBuffer.Get());
	m_deviceWrapper.Present();
}

void D3D12App::OnSampleSubmit(ID3D12Resource &backBuffer)
{
#if ENABLE_UI
	if (m_bIsRecording)
	{
		m_SamplesRendered++;
		if (m_SamplesRendered == m_pUIController->GetCaptureSamplesPerFrame())
		{
			std::wstring filename = L"frame" + std::to_wstring(m_FramesRendered) + L".png";
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
#endif
}
