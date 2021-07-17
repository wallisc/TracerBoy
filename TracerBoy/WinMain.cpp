#include "pch.h"

#if ENABLE_UI
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 4;}

extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }

std::unique_ptr<D3D12App> g_pD3D12App;
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
	auto WindowName = "TracerBoy";

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
	g_pD3D12App = std::unique_ptr<D3D12App>(new D3D12App(g_hwnd, pCommandLine));

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