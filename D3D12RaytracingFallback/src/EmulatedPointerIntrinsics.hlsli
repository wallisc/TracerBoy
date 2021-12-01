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
#include "EmulatedPointer.hlsli"

RWByteAddressBuffer PointerGetBuffer(GpuVA address)
{
#if FAST_PATH
    return DescriptorHeapBufferTable[address[EmulatedPointerDescriptorHeapIndex]];
#else
    return DescriptorHeapBufferTable[NonUniformResourceIndex(address[EmulatedPointerDescriptorHeapIndex])];
#endif
}

uint PointerGetBufferStartOffset(GpuVA address)
{
    return address[EmulatedPointerOffsetIndex];
}

uint4 Load4(GpuVA address)
{
    return PointerGetBuffer(address).Load4(PointerGetBufferStartOffset(address));
}

RWByteAddressBufferPointer CreateRWByteAddressBufferPointerFromGpuVA(GpuVA address)
{
    return CreateRWByteAddressBufferPointer(PointerGetBuffer(address), PointerGetBufferStartOffset(address));
}



