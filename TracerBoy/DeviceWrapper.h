#pragma once

class DeviceWrapper
{
public:
	virtual ID3D12Device& GetDevice() = 0;
	virtual ID3D12CommandQueue& GetPresentQueue() = 0;
	virtual UINT GetBackBufferIndex() = 0;
	virtual ID3D12Resource& GetBackBuffer(UINT) = 0;
	virtual void Present() = 0;

	virtual bool GetWin32ExtensionData(void* HWND, void** ppDXGISwapChain) { return false; }
};