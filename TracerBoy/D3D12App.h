#pragma once

class D3D12App
{
public:

	D3D12App(HWND hwnd, LPSTR pCommandLine);

	void UpdateMousePosition(int x, int y);
	void KeyUpEvent(char key) { m_inputArray[key] = false; }
	void KeyDownEvent(char key) { m_inputArray[key] = true; }
	
	void Render();
private:
	int m_mouseX, m_mouseY;
	bool m_inputArray[CHAR_MAX];
	std::chrono::steady_clock::time_point m_LastUpdateTime;

	CComPtr<IDXGISwapChain3> m_pSwapChain;
	CComPtr<ID3D12Device> m_pDevice;
	CComPtr<ID3D12CommandQueue> m_pCommandQueue;

	std::unique_ptr<TracerBoy> m_pTracerBoy;
};