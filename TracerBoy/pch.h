#pragma once

#define ENABLE_UI 1
#define USE_ASSIMP 1
#define USE_OPENVDB 0
#define SUPPORT_SW_RAYTRACING 1
#define USE_XESS 0
#define USE_DLSS 1

#include <memory>
#include <deque>
#include <string>
#include <chrono>
#include <unordered_map>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#define IS_WINDOWS 1

#include <windows.h>
#include <windowsx.h>
#include <wrl/client.h>

#if IS_WINDOWS
#include <d3d12.h>
#include <dxgi1_6.h>
#include "d3dx12.h"
#define D3D12_ROOT_SIGNATURE_FLAG_RAYTRACING D3D12_ROOT_SIGNATURE_FLAG_NONE
#endif


#include "directxtex.h"
#include "directxmath.h"

#include "wincodec.h"

#define HANDLE_FAILURE() throw -1;
#define VERIFY(x) if(!(x)) HANDLE_FAILURE();
#define VERIFY_HRESULT(x) VERIFY(SUCCEEDED(x))

#ifndef IID_GRAPHICS_PPV_ARGS
#define IID_GRAPHICS_PPV_ARGS(x) IID_PPV_ARGS(x)
#endif

using Microsoft::WRL::ComPtr;
using namespace DirectX;

#ifdef WIN32
#undef min
#undef max
#endif

// There's CPU overhead to using pix on release but I don't care about CPU :)
#define USE_PIX
#include "pix3.h"

#include "PBRTParser\Scene.h"
#if USE_ASSIMP
#include <assimp/Importer.hpp>      // C++ importer interface
#include <assimp/postprocess.h>     // Post processing flags
#include <assimp/scene.h>
#include "AssimpImporter.h"
#endif

#include "RazerChromaManager.h"

#define _USE_MATH_DEFINES // for C++
#include <cmath>

#if USE_OPENVDB
#include "openvdb/openvdb.h"
#endif

#define DEFAULT_CAMERA_SPEED 0.003f

#if SUPPORT_SW_RAYTRACING
#include "D3D12RaytracingFallback.h"
#endif

#if USE_XESS
#include "xess/xess_d3d12.h"
#include "xess/xess_debug.h"
#endif 

#if USE_DLSS
#include "dlss/nvsdk_ngx_helpers.h"

#define NV_VERIFY(x) \
{																										\
	NVSDK_NGX_Result nvResult = x;																		\
	if(nvResult != NVSDK_NGX_Result_Success)															\
	{																									\
		std::wstring ngxErrorWString = L"NGX error: " + std::wstring(GetNGXResultAsString(nvResult));	\
		std::string ngxErrorString(ngxErrorWString.begin(), ngxErrorWString.end());						\
		OutputDebugString(ngxErrorString.c_str());														\
		HANDLE_FAILURE();																				\
	}																									\
}

#endif

#include "SharedShaderStructs.h"
#include "DenoiserPass.h"
#include "TemporalAccumulationPass.h"
#include "FidelityFXSuperResolution.h"
#include "TracerBoy.h"
#include "UIController.h"
#include "DeviceWrapper.h"
#include "D3D12App.h"
#include "Curves.h"

#if ENABLE_UI
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#endif
