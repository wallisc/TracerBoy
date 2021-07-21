#include "../TracerBoy/pch.h"

#if ENABLE_UI
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 4;}

extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }

class WindowsDevice : public DeviceWrapper
{
public:
	WindowsDevice(HWND hwnd, UINT numBackBuffers) : m_hwnd(hwnd)
	{
#if 0
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_GRAPHICS_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();
		}
#endif

		VERIFY_HRESULT(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_GRAPHICS_PPV_ARGS(&m_pDevice)));

		D3D12_COMMAND_QUEUE_DESC createCommandQueueDesc = {};
		createCommandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		createCommandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		VERIFY_HRESULT(m_pDevice->CreateCommandQueue(&createCommandQueueDesc, IID_GRAPHICS_PPV_ARGS(&m_pCommandQueue)));

		ComPtr<IDXGIFactory2> pDxgiFactory2;
		VERIFY_HRESULT(CreateDXGIFactory2(0, IID_GRAPHICS_PPV_ARGS(&pDxgiFactory2)));

		RECT clientRect;
		if (!GetClientRect(hwnd, &clientRect)) HANDLE_FAILURE();

		ComPtr<IDXGISwapChain1> pSwapChain;
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.BufferCount = numBackBuffers;
		swapChainDesc.Width = clientRect.right - clientRect.left;
		swapChainDesc.Height = clientRect.bottom - clientRect.top;
		swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
		VERIFY_HRESULT(pDxgiFactory2->CreateSwapChainForHwnd(m_pCommandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr, &pSwapChain));

		VERIFY_HRESULT(pSwapChain.As(&m_pSwapChain));
	}

	ID3D12Device& GetDevice()
	{
		return *m_pDevice.Get();
	}

	ID3D12CommandQueue& GetPresentQueue()
	{
		return *m_pCommandQueue.Get();
	}

	UINT GetBackBufferIndex()
	{
		return m_pSwapChain->GetCurrentBackBufferIndex();
	}

	ID3D12Resource& GetBackBuffer(UINT backBufferIndex)
	{
		ComPtr<ID3D12Resource> pBackBuffer;
		m_pSwapChain->GetBuffer(backBufferIndex, IID_GRAPHICS_PPV_ARGS(&pBackBuffer));
		return *pBackBuffer.Get();
	}

	void Present()
	{
		m_pSwapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
	}

	bool GetWin32ExtensionData(void* pHWND, void** ppDXGISwapChain) 
	{ 
		*(HWND *)pHWND = m_hwnd;
		*(IDXGISwapChain3**)ppDXGISwapChain = m_pSwapChain.Get();
		return true; 
	}


private:
	HWND m_hwnd;
	ComPtr<IDXGISwapChain3> m_pSwapChain;
	ComPtr<ID3D12CommandQueue> m_pCommandQueue;
	ComPtr<ID3D12Device> m_pDevice;
};

std::unique_ptr<D3D12App> g_pD3D12App;
std::unique_ptr<WindowsDevice> g_pWindowsDevice;
HWND g_hwnd;
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
#if ENABLE_UI
	if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
		return true;
#endif

	switch (message)
	{
	case WM_KEYDOWN:
		{
			TCHAR wchar = wParam;
			if (wchar < CHAR_MAX)
			{
				g_pD3D12App->KeyDownEvent(wchar);
			}
		}
		return 0;
	case WM_KEYUP:
		{
			TCHAR wchar = wParam;
			if (wchar < CHAR_MAX)
			{
				g_pD3D12App->KeyUpEvent(wchar);
			}
		}
		return 0;
	case WM_PAINT:
		{
			g_pD3D12App->Render();
		}
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_MOUSEMOVE:
		//if (wParam & MK_LBUTTON)
		{
			g_pD3D12App->UpdateMousePosition(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		}
		return 0;
	}

	// Handle any messages the switch statement didn't.
	return DefWindowProc(hWnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR pCommandLine, int nCmdShow)
{
	auto WindowName = L"TracerBoy";

	WNDCLASSEX windowClass = { 0 };
	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = hInstance;
	windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	windowClass.lpszClassName = WindowName;
	RegisterClassEx(&windowClass);

	RECT windowRect = { 0, 0, 2560, 1440};
	AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

	g_hwnd = CreateWindow(
		windowClass.lpszClassName,
		WindowName,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		nullptr,
		nullptr,
		hInstance,
		nullptr);

	ShowWindow(g_hwnd, nCmdShow);

	g_pWindowsDevice = std::unique_ptr<WindowsDevice>(new WindowsDevice(g_hwnd, 2));
	g_pD3D12App = std::unique_ptr<D3D12App>(new D3D12App(*g_pWindowsDevice, pCommandLine));

	MSG msg = {};
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	return static_cast<char>(msg.wParam);
}