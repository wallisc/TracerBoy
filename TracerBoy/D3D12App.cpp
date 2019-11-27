#include "pch.h"

D3D12App::D3D12App(HWND hwnd, LPSTR pCommandLine)
{
#ifdef DEBUG
	CComPtr<ID3D12Debug> debugController;
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

	CComPtr<IDXGIFactory2> pDxgiFactory2;
	VERIFY_HRESULT(CreateDXGIFactory2(0, IID_PPV_ARGS(&pDxgiFactory2)));

	RECT clientRect;
	if (!GetClientRect(hwnd, &clientRect)) HANDLE_FAILURE();

	CComPtr<IDXGISwapChain1> pSwapChain;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = 2;
	swapChainDesc.Width = clientRect.right - clientRect.left;
	swapChainDesc.Height = clientRect.bottom - clientRect.top;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	swapChainDesc.SampleDesc.Count = 1;
	VERIFY_HRESULT(pDxgiFactory2->CreateSwapChainForHwnd(m_pCommandQueue, hwnd, &swapChainDesc, nullptr, nullptr, &pSwapChain));

	VERIFY_HRESULT(pSwapChain->QueryInterface(&m_pSwapChain));
	std::string commandLine(pCommandLine);
	m_pTracerBoy = std::unique_ptr<TracerBoy>(new TracerBoy(m_pCommandQueue, commandLine));
}

void D3D12App::UpdateMousePosition(int x, int y)
{
	m_pTracerBoy->UpdateMouseInput(x, y);
}

void D3D12App::Render()
{
	CComPtr<ID3D12Resource> pBackBuffer;
	m_pSwapChain->GetBuffer(m_pSwapChain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&pBackBuffer));

	m_pTracerBoy->Render(pBackBuffer);

	m_pSwapChain->Present(0, 0);
}