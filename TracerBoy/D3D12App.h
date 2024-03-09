#pragma once

class D3D12App
{
public:
	const static UINT cNumBackBuffers = 2;

	D3D12App(DeviceWrapper &deviceWrapper, LPCSTR pCommandLine);

	~D3D12App() 
	{
		WaitForGPUIdle();
	};


	void UpdateMousePosition(int x, int y);
	void NotifyLeftMouseClick();

	void KeyUpEvent(char key) 
	{ 
		m_inputArray[key] = false; 
		if (key == 'm' || key == 'M')
		{
			m_MouseMovementEnabled = !m_MouseMovementEnabled;
		}
		if(key == 'p' || key == 'P')
		{ 
			StartRecording();
		}
	}
	void KeyDownEvent(char key) { m_inputArray[key] = true; }
	
	void Render();
private:
	void LoadScene(const std::string &commandLine);
	void InitializeTracerBoy(const std::string& commandLine);

	typedef std::pair<ComPtr<ID3D12GraphicsCommandList>, ComPtr<ID3D12CommandAllocator>> CommandListAllocatorPair;
	void AcquireCommandListAllocatorPair(CommandListAllocatorPair& pair);
	UINT ExecuteAndFreeCommandListAllocatorPair(CommandListAllocatorPair& pair);

	UINT64 SignalFence();
	void WaitForGPUIdle();
	void WaitForFenceValue(UINT fenceValue);

	void StartRecording()
	{
		m_bIsRecording = true;
		m_bRenderUI = false;
		m_SamplesRendered = 0;
		m_FramesRendered = 0;
	}

	void OnSampleSubmit(ID3D12Resource& backBuffer);

	void StopRecording()
	{
		m_bRenderUI = true;
		m_bIsRecording = false;
	}

	UINT m_FrameFence[cNumBackBuffers] = {};
	ComPtr<ID3D12Resource> m_pReadbackStatBuffers[cNumBackBuffers];

	std::mutex CommandListAllocatorPairMutex;
	ComPtr<ID3D12Fence> m_pFence;
	UINT64 m_SignalValue;
	std::deque<std::pair<CommandListAllocatorPair, UINT64>> FreedCommandListAllocatorPairs;

	SceneLoadStatus m_sceneLoadStatus;
	std::mutex m_sceneLoadStatusUpdateLock;
	std::thread m_asyncLoadSceneThread;
	std::atomic<bool> bIsSceneLoadFinished;
	bool m_bRenderUI;

	bool m_bValidPixelSelection = false;
	UINT m_FramesSincePixelSelection = 0;

	bool m_bIsRecording;
	UINT m_SamplesRendered;
	UINT m_FramesRendered;

	bool m_MouseMovementEnabled = false;
	int m_mouseX, m_mouseY;
	bool m_inputArray[CHAR_MAX];
	std::chrono::steady_clock::time_point m_LastUpdateTime;
	std::chrono::steady_clock::time_point m_TimeSinceLastInvalidate;
	std::chrono::steady_clock::time_point m_TimeSinceConvergence;

	ComPtr<ID3D12Device> m_pDevice;
	ComPtr<ID3D12CommandQueue> m_pCommandQueue;
	DeviceWrapper& m_deviceWrapper;

	RazerChromaManager m_razerChromaManager;

	std::unique_ptr<TracerBoy> m_pTracerBoy;
#if ENABLE_UI
	std::unique_ptr<UIController> m_pUIController;
#endif
};