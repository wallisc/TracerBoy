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

	float fogScatterDistance;
	float fogScatterDirection;
	uint GlobalFrameCount;
	float MinConvergence;

	float VarianceMultplier;
	float3 VolumeMin;
	
	float3 VolumeMax;
	uint UseBlueNoise;

	uint UseExecuteIndirect;
};

struct ConfigConstants
{
	float CameraLensHeight;
	uint FlipTextureUVs;

	float2 Padding;

	float4x3 EnvironmentMapTransform;
};

struct Vertex
{
	float3 Position;
	float3 Normal;
	float2 UV;
	float3 Tangent;
};

#define DEFAULT_MATERIAL_FLAG 0x0
#define METALLIC_MATERIAL_FLAG 0x1
#define SUBSURFACE_SCATTER_MATERIAL_FLAG 0x2
#define NO_SPECULAR_MATERIAL_FLAG 0x4
#define MIX_MATERIAL_FLAG 0x8
#define LIGHT_MATERIAL_FLAG 0x10

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
	float roughness;
	float absorption;
	float scattering;

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

struct AreaLightData
{
	float3 Emissive;
	float3 TopLeft;
	float3 TopRight;
	float3 BottomLeft;
	float3 BottomRight;
};

#define RAYTRACE_THREAD_GROUP_HEIGHT 8
#define RAYTRACE_THREAD_GROUP_WIDTH 8