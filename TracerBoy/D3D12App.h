#pragma once

class D3D12App
{
public:
	const static UINT cNumBackBuffers = 2;

	D3D12App(HWND hwnd, LPSTR pCommandLine);

	~D3D12App() 
	{
		WaitForGPUIdle();
	};


	void UpdateMousePosition(int x, int y);
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

	UINT m_FrameFence[cNumBackBuffers];
	ComPtr<ID3D12Fence> m_pFence;
	UINT64 m_SignalValue;
	std::deque<std::pair<CommandListAllocatorPair, UINT64>> FreedCommandListAllocatorPairs;

	bool m_bRenderUI;


	bool m_bIsRecording;
	UINT m_SamplesRendered;
	UINT m_FramesRendered;

	bool m_MouseMovementEnabled = false;
	int m_mouseX, m_mouseY;
	bool m_inputArray[CHAR_MAX];
	std::chrono::steady_clock::time_point m_LastUpdateTime;

	ComPtr<IDXGISwapChain3> m_pSwapChain;
	ComPtr<ID3D12Device> m_pDevice;
	ComPtr<ID3D12CommandQueue> m_pCommandQueue;

	std::unique_ptr<TracerBoy> m_pTracerBoy;
	std::unique_ptr<UIController> m_pUIController;
};