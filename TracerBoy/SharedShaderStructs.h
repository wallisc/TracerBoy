#ifdef HLSL
#define UINT_MAX 4294967295
#else
#pragma once
struct float2 { float x; float y; };
struct float3 { float x; float y; float z; };
typedef UINT uint;
struct uint2 { uint x; uint y; };
#endif

struct RayPayload
{
	float2 uv;
	uint materialIndex;
	float hitT;
	float3 normal;
};

struct PerFrameConstants
{
	float3 CameraPosition;
	float Time;
	float3 CameraLookAt;
	uint InvalidateHistory;
	float3 CameraUp;
	float Padding;
	float3 CameraRight;
};

struct ConfigConstants
{
	float FocalDistance;
	float CameraLensHeight;
};

struct Vertex
{
	float3 Position;
	float3 Normal;
	float2 UV;
};

#define DEFAULT_MATERIAL_FLAG 0x0
#define METALLIC_MATERIAL_FLAG 0x1
#define SUBSURFACE_SCATTER_MATERIAL_FLAG 0x2
#define NO_SPECULAR_MATERIAL_FLAG 0x4
#define MIX_MATERIAL_FLAG 0x8
#define LIGHT_MATERIAL_FLAG 0x10

struct Material
{
	float3 albedo;
	uint albedoIndex;
	
	float IOR;
	float roughness;
	float absorption;
	float scattering;

	float3 emissive;
	int Flags;
};

#define IMAGE_TEXTURE_TYPE 0

struct TextureData
{
	uint TextureType;
	uint DescriptorHeapIndex;
};

struct AreaLightData
{
	float3 Emissive;
	float3 TopLeft;
	float3 TopRight;
	float3 BottomLeft;
	float3 BottomRight;
};