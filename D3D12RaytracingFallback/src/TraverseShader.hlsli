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
#include "HLSLRaytracingInternalPrototypes.h"

#define HLSL

#include "UberShaderBindings.h"
#include "DebugLog.h"
#include "RayTracingHelper.hlsli"
#include "EmulatedPointerIntrinsics.hlsli"
#include "TraverseFunction.hlsli"