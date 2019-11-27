#pragma once

class D3D12App
{
public:

	D3D12App(HWND hwnd, LPSTR pCommandLine);

	void UpdateMousePosition(int x, int y);
	void Render();
private:
	CComPtr<IDXGISwapChain3> m_pSwapChain;
	CComPtr<ID3D12Device> m_pDevice;
	CComPtr<ID3D12CommandQueue> m_pCommandQueue;

	std::unique_ptr<TracerBoy> m_pTracerBoy;
};