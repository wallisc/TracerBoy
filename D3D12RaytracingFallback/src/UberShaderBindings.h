//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************
#pragma once
#include "FallbackDebug.h"
#include "FallbackDxil.h"

#ifndef HLSL
#include "HlslCompat.h"
#else
#include "ShaderUtil.hlsli"
#endif

#define FallbackLayerDescriptorHeapRegisterSpace 214743648
#ifdef HLSL
cbuffer AccelerationStructureList : CONSTANT_REGISTER_SPACE(FallbackLayerAccelerationStructureList, FallbackLayerRegisterSpace)
{
    uint2 TopLevelAccelerationStructureGpuVA;
};


RWByteAddressBuffer DescriptorHeapBufferTable[] : UAV_REGISTER_SPACE(FallbackLayerDescriptorHeapTable, FallbackLayerDescriptorHeapRegisterSpace);
#else
static_assert(FallbackLayerDescriptorHeapRegisterSpace == FallbackLayerRegisterSpace + FallbackLayerDescriptorHeapSpaceOffset, L"#define for FallbackLayerDescriptorHeapRegisterSpace is incorrect");
#endif
