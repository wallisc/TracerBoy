#pragma once

class D3D12App
{
public:
	const static UINT cNumBackBuffers = 2;

	D3D12App(HWND hwnd, LPSTR pCommandLine);

	void UpdateMousePosition(int x, int y);
	void KeyUpEvent(char key) { m_inputArray[key] = false; }
	void KeyDownEvent(char key) { m_inputArray[key] = true; }
	
	void Render();
private:
	void InitImigui(HWND hwnd);

	typedef std::pair<ComPtr<ID3D12GraphicsCommandList>, ComPtr<ID3D12CommandAllocator>> CommandListAllocatorPair;
	void AcquireCommandListAllocatorPair(CommandListAllocatorPair& pair);
	UINT ExecuteAndFreeCommandListAllocatorPair(CommandListAllocatorPair& pair);

	UINT64 SignalFence();
	void WaitForGPUIdle();
	void WaitForFenceValue(UINT fenceValue);

	UINT m_FrameFence[cNumBackBuffers];
	ComPtr<ID3D12Fence> m_pFence;
	UINT64 m_SignalValue;
	std::deque<std::pair<CommandListAllocatorPair, UINT64>> FreedCommandListAllocatorPairs;

	int m_mouseX, m_mouseY;
	bool m_inputArray[CHAR_MAX];
	std::chrono::steady_clock::time_point m_LastUpdateTime;

	ComPtr<IDXGISwapChain3> m_pSwapChain;
	ComPtr<ID3D12Device> m_pDevice;
	ComPtr<ID3D12CommandQueue> m_pCommandQueue;
	ComPtr<ID3D12DescriptorHeap> m_pImguiSRVDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap> m_pImguiRTVDescriptorHeap;

	D3D12_CPU_DESCRIPTOR_HANDLE m_RTV[cNumBackBuffers];

	std::unique_ptr<TracerBoy> m_pTracerBoy;
};