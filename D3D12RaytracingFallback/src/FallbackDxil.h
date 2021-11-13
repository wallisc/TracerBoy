#pragma once

#define FallbackLayerRegisterSpace 214743647


// SRVs
#define FallbackLayerHitGroupRecordByteAddressBufferRegister 0
#define FallbackLayerMissShaderRecordByteAddressBufferRegister 1
#define FallbackLayerRayGenShaderRecordByteAddressBufferRegister 2
#define FallbackLayerCallableShaderRecordByteAddressBufferRegister 3

// SRV & UAV
#define FallbackLayerDescriptorHeapTable 0

// There's a driver issue on some hardware that has issues
// starting a bindless table on any register but 0, so
// make sure each bindless table has it's own register space
#define FallbackLayerDescriptorHeapSpaceOffset 1
#define FallbackLayerNumDescriptorHeapSpacesPerView 10

// CBVs
#define FallbackLayerDispatchConstantsRegister 0
#define FallbackLayerAccelerationStructureList 1

#ifndef HLSL
struct ViewKey {
    unsigned int ViewType;
    unsigned int StructuredStride;
};

struct ShaderInfo
{
    const wchar_t *ExportName;
    unsigned int SamplerDescriptorSizeInBytes;
    unsigned int SrvCbvUavDescriptorSizeInBytes;
    unsigned int ShaderRecordIdentifierSizeInBytes;
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *pRootSignatureDesc;

    ViewKey *pSRVRegisterSpaceArray;
    UINT *pNumSRVSpaces;

    ViewKey *pUAVRegisterSpaceArray;
    UINT *pNumUAVSpaces;
};

struct DispatchRaysConstants
{
    unsigned __int32 RayDispatchDimensionsWidth;
    unsigned __int32 RayDispatchDimensionsHeight;
    unsigned __int32 HitGroupShaderRecordStride;
    unsigned __int32 MissShaderRecordStride;

    // 64-bit values
    unsigned __int64 SamplerDescriptorHeapStart;
    unsigned __int64 SrvCbvUavDescriptorHeapStart;
};

enum DescriptorRangeTypes
{
    SRV = 0,
    CBV,
    UAV,
    Sampler,
    NumRangeTypes
};

enum RootSignatureParameterOffset
{
    CbvSrvUavDescriptorHeapAliasedTables = 0,
    AccelerationStructuresList,
    NumParameters
};
#endif
