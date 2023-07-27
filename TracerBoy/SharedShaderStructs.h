#ifdef HLSL
#define UINT_MAX 4294967295
#define EPSILON 0.0001f
#else
#pragma once
struct float2 { float x; float y; };
struct float3 { float x; float y; float z; };
struct float4 { float x; float y; float z; float w; };
struct float4x3
{ 
	float4 vx;
	float4 vy;
	float4 vz;
};
typedef UINT uint;
struct uint2 { uint x; uint y; };

#endif

#define IS_Y_AXIS_UP 1

struct RayPayload
{
	float2 uv;
	uint materialIndex;
	float hitT;
	
	float3 normal;
	float padding;
	
	float3 tangent;
	float padding2;
};

struct PerFrameConstants
{
	float3 CameraPosition;
	float Time;

	float3 CameraLookAt;
	uint InvalidateHistory;

	float3 CameraUp;
	uint OutputMode;
	
	float3 CameraRight;
	float DOFFocusDistance;

	float DOFApertureWidth;
	uint EnableNormalMaps;
	float FocalDistance;
	float FireflyClampValue;

	uint GlobalFrameCount;
	float MinConvergence;
	uint LightCount;

	uint UseBlueNoise;
	uint IsRealTime;
	float DebugValue;
	float DebugValue2;
};

struct ConfigConstants
{
	float CameraLensHeight;
	uint FlipTextureUVs;

	float2 Padding;

	float4x3 EnvironmentMapTransform;
	float3 EnvironmentMapColorScale;
};

struct Vertex
{
	float3 Normal;
	float2 UV;
	float3 Tangent;
};

struct Light
{
	float3 LightColor;
	float SurfaceArea;
	float3 P0, P1, P2;
	float3 N0, N1, N2;
};

#define DEFAULT_MATERIAL_FLAG 0x0
#define METALLIC_MATERIAL_FLAG 0x1
#define SUBSURFACE_SCATTER_MATERIAL_FLAG 0x2
#define NO_SPECULAR_MATERIAL_FLAG 0x4
#define MIX_MATERIAL_FLAG 0x8
#define LIGHT_MATERIAL_FLAG 0x10
#define NO_ALPHA_MATERIAL_FLAG 0x20
#define HAIR_MATERIAL_FLAG 0x40

#define OUTPUT_TYPE_LIT 0
#define OUTPUT_TYPE_ALBEDO 1
#define OUTPUT_TYPE_NORMAL 2
#define OUTPUT_TYPE_LUMINANCE 3
#define OUTPUT_TYPE_VARIANCE 4
#define OUTPUT_TYPE_LIVE_PIXELS 5
#define OUTPUT_TYPE_LIVE_WAVES 6


struct Material
{
	float3 albedo;
	uint albedoIndex;

	uint alphaIndex;
	uint normalMapIndex;
	uint emissiveIndex;
	uint specularMapIndex;

	float IOR;
	float3 absorption;

	float roughness;
	float3 scattering;

	float3 emissive;
	int Flags;

	float SpecularCoef;
};

#define IMAGE_TEXTURE_TYPE 0
#define CHECKER_TEXTURE_TYPE 1

#define DEFAULT_TEXTURE_FLAG 0
#define NEEDS_GAMMA_CORRECTION_TEXTURE_FLAG 0x1
struct TextureData
{
	uint TextureType;
	uint DescriptorHeapIndex;
	uint TextureFlags;
	uint Padding;

	// TODO: these shoudl be in some type of union
	float3 CheckerColor1;
	float UScale;

	float3 CheckerColor2;
	float VScale;

};

#define RAYTRACE_THREAD_GROUP_HEIGHT 8
#define RAYTRACE_THREAD_GROUP_WIDTH 8