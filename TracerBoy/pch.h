#pragma once

#include <memory>
#include <deque>
#include <string>
#include <chrono>
#include <unordered_map>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <windowsx.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include "d3dx12.h"

#include "directxtex.h"
#include "directxmath.h"

#define HANDLE_FAILURE() throw -1;
#define VERIFY(x) if(!(x)) HANDLE_FAILURE();
#define VERIFY_HRESULT(x) VERIFY(SUCCEEDED(x))

using Microsoft::WRL::ComPtr;
using namespace DirectX;

#include "TracerBoy.h"
#include "D3D12App.h"