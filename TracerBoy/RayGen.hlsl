#define HLSL
#include "SharedShaderStructs.h"

#define IS_SHADER_TOY 0
cbuffer PerFrameCB : register(b0)
{
	PerFrameConstants perFrameConstants;
}

cbuffer ConfigCB : register(b1)
{
	ConfigConstants configConstants;
}

bool ShouldInvalidateHistory() { return perFrameConstants.InvalidateHistory; }
float3 GetCameraPosition() { return perFrameConstants.CameraPosition; }
float3 GetCameraLookAt() { return perFrameConstants.CameraLookAt; }
float3 GetCameraUp() { return perFrameConstants.CameraUp; }
float3 GetCameraRight() { return perFrameConstants.CameraRight; }
float GetCameraLensHeight() { return configConstants.CameraLensHeight; }
float GetCameraFocalDistance() { return configConstants.FocalDistance; }

RWTexture2D<float4> OutputTexture : register(u0);
Texture2D LastFrameTexture : register(t0);
RaytracingAccelerationStructure AS : register(t1);
Texture2D EnvironmentMap : register(t4);
StructuredBuffer<float> RandSeedBuffer : register(t5);
StructuredBuffer<Material> MaterialBuffer : register(t6);
StructuredBuffer<TextureData> TextureDataBuffer : register(t7);
Texture2D<float4> ImageTextures[] : register(t0, space1);

SamplerState PointSampler : register(s0);
SamplerState BilinearSampler : register(s1);

float3 SampleEnvironmentMap(float3 v)
{
	float3 viewDir = normalize(v);
	float2 uv;
	float p = atan2(viewDir.z, viewDir.x);
	p = p > 0 ? p : p + 2 * 3.14;
	uv.x = p / (2 * 3.14);
	uv.y = acos(viewDir.y) / (3.14);

	return EnvironmentMap.SampleLevel(BilinearSampler, uv, 0).rgb;
}

float rand();

void GetOneLightSample(out float3 LightPosition, out float3 LightColor, out float PDFValue)
{
	float2 areaLightUV = float2(rand() * 2.0 - 1.0, rand() * 2.0 - 1.0);
	LightPosition = float3(0.5f, 7.14059f, -1.5f) +
		float3(1.0, 0.0, 0.0) * areaLightUV.x +
		float3(0.0, 0.0, 1.0) * areaLightUV.y;
	LightColor = float3(30.0, 30.0, 30.0);
	PDFValue = 0.0;
}

#define GLOBAL static
float GetTime() { return perFrameConstants.Time; }
float3 GetResolution() { return float3(DispatchRaysDimensions()); }
float4 GetMouse() { return float4(0.0, 0.0, 0.0, 0.0); }
float4 GetLastFrameData() {
	return LastFrameTexture.SampleLevel(PointSampler, float2(0, 1), 0);
}
float4 GetAccumulatedColor(float2 uv) {
	uv.y = 1.0 - uv.y;
	return LastFrameTexture.SampleLevel(PointSampler, uv, 0);
}
bool NeedsToSaveLastFrameData() { return false; } // Handled by the CPU

Material GetMaterial_NonRecursive(int MaterialID)
{
	return MaterialBuffer[MaterialID];
}

float3 GetTextureData(uint textureIndex, float2 uv)
{
	 uv = float2(0, 1) + uv * float2(1, -1);

	float3 data = float3(0.0, 0.0, 0.0);
	TextureData textureData = TextureDataBuffer[textureIndex];
	if (textureData.TextureType == IMAGE_TEXTURE_TYPE)
	{
		data = ImageTextures[NonUniformResourceIndex(textureData.DescriptorHeapIndex)].SampleLevel(BilinearSampler, uv, 0).rgb;
	}
	return data;
}

Material GetMaterial(int MaterialID, uint PrimitiveID, float3 WorldPosition, float2 uv)
{
	Material mat = MaterialBuffer[MaterialID];
	if ((mat.Flags & MIX_MATERIAL_FLAG) != 0)
	{
		if (rand() < mat.albedo.z)
		{
			return GetMaterial_NonRecursive(uint(mat.albedo.x));
		}
		else
		{
			return GetMaterial_NonRecursive(uint(mat.albedo.y));
		}
	}

	if(mat.albedoIndex != UINT_MAX)
	{
		mat.albedo = GetTextureData(mat.albedoIndex, uv);
	}

	return mat;
}

struct Ray
{
	float3 origin;
	float3 direction;
};

float2 IntersectWithMaxDistance(Ray ray, float maxT, out float3 normal, out float2 uv, out uint PrimitiveID)
{
	RayDesc dxrRay;
	dxrRay.Origin = ray.origin;
	dxrRay.Direction = ray.direction;
	dxrRay.TMin = 0.001;
	dxrRay.TMax = maxT;

	float bigNumber = 9999.0f;
	RayPayload payload = { float2(0, 0), uint(-1), bigNumber, float3(0, 0, 0) };
	TraceRay(AS, RAY_FLAG_NONE, ~0, 0, 1, 0, dxrRay, payload);

	float2 result;
	result.x = payload.hitT;
	bool hitFound = result.x < bigNumber;
	if (hitFound)
	{
		result.y = payload.materialIndex;
	}
	else
	{
		result.y = -1;
	}
	PrimitiveID = 0;
	normal = payload.normal;
	uv = payload.uv;
	return result;
}

#include "GLSLCompat.h"
#include "kernel.glsl"

[shader("raygeneration")]
void RayGen()
{
	seed = RandSeedBuffer[DispatchRaysIndex().x + DispatchRaysIndex().y * DispatchRaysDimensions().x];
	float2 dispatchUV = float2(DispatchRaysIndex().xy + 0.5) / float2(DispatchRaysDimensions().xy);
	float2 uv = vec2(0, 1) + dispatchUV * vec2(1, -1);
	OutputTexture[DispatchRaysIndex().xy] = PathTrace(uv * GetResolution().xy);
}
