#pragma once

#define ENABLE_UI 1
#define USE_ASSIMP 1
#define USE_OPENVDB 0

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

#include "wincodec.h"
#include "xinput.h"

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

#define _USE_MATH_DEFINES // for C++
#include <cmath>

#if USE_OPENVDB
#include "openvdb/openvdb.h"
#endif

#define DEFAULT_CAMERA_SPEED 0.003f

#include "SharedShaderStructs.h"
#include "DenoiserPass.h"
#include "TemporalAccumulationPass.h"
#include "TracerBoy.h"
#include "UIController.h"
#include "D3D12App.h"

#if ENABLE_UI
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#endif
