#define HLSL
#include "SharedShaderStructs.h"
#include "SharedRaytracing.h"
#include "Tonemap.h"

#define IS_SHADER_TOY 0


bool ShouldInvalidateHistory() { return perFrameConstants.InvalidateHistory; }
float3 GetCameraPosition() { return perFrameConstants.CameraPosition; }
float3 GetCameraLookAt() { return perFrameConstants.CameraLookAt; }
float3 GetCameraUp() { return perFrameConstants.CameraUp; }
float3 GetCameraRight() { return perFrameConstants.CameraRight; }
float GetCameraLensHeight() { return configConstants.CameraLensHeight; }
float GetCameraFocalDistance() { return perFrameConstants.FocalDistance; }


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
	LightPosition = float3(0.172, -0.818, -0.549) * -1000.0f;
	LightPosition.xz += float2(rand() * 2.0 - 1.0, rand() * 2.0 - 1.0) * 100.0f;

	LightColor = float3(1.0, 1.0, 1.0) * 0.5;
	PDFValue = 0.0;
}

#define GLOBAL static
float GetTime() { return perFrameConstants.Time; }
float3 GetResolution() { return float3(DispatchRaysDimensions()); }
float4 GetMouse() { return float4(0.0, 0.0, 0.0, 0.0); }
float4 GetLastFrameData() {
	return OutputTexture[float2(0, DispatchRaysDimensions().y - 1)];
}
float4 GetAccumulatedColor(float2 uv) 
{
	return OutputTexture[DispatchRaysIndex().xy];
}
bool NeedsToSaveLastFrameData() { return false; } // Handled by the CPU

float3 GetDetailNormal(Material mat, float3 normal, float3 tangent, float2 uv)
{
	if (IsValidTexture(mat.normalMapIndex) && perFrameConstants.EnableNormalMaps)
	{
		float3 bitangent = cross(tangent, normal);
		float2 normalMapData = GetTextureData(mat.normalMapIndex, uv).xy;
		float3 tbn = float3(
			(0.5f - normalMapData.x) * 2.0f,
			(0.5f - normalMapData.y) * 2.0f,
			0.0f);
		tbn.z = sqrt(1.0f - (tbn.x * tbn.x + tbn.y * tbn.y));

		// Prevent normal maps from making a normal completely parallel with the triangle geometry. This causes
		// artifacts due to impossible reflection rays
		const float normalYClamp = 0.02f;

		return normalize(
			tangent * tbn.x +
			bitangent * tbn.y +
			normal * max(tbn.z, normalYClamp));
	}
	return normal;
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

	if(IsValidTexture(mat.albedoIndex))
	{
		mat.albedo = GetTextureData(mat.albedoIndex, uv);
	}

	if (IsValidTexture(mat.emissiveIndex))
	{
		mat.emissive = GetTextureData(mat.emissiveIndex, uv);
	}

	if (IsValidTexture(mat.specularMapIndex))
	{
		float3 specularMapData = GetTextureData(mat.specularMapIndex, uv);
		mat.roughness = specularMapData.g;
		if (specularMapData.b > 0.5f)
		{
			mat.Flags |= METALLIC_MATERIAL_FLAG;
		}
	}

	return mat;
}

struct Ray
{
	float3 origin;
	float3 direction;
};

float2 IntersectWithMaxDistance(Ray ray, float maxT, out float3 normal, out float3 tangent, out float2 uv, out uint PrimitiveID)
{
	RayDesc dxrRay;
	dxrRay.Origin = ray.origin;
	dxrRay.Direction = ray.direction;
	dxrRay.TMin = 0.001;
	dxrRay.TMax = maxT;

	RayPayload payload = { float2(0, 0), uint(-1), maxT, float3(0, 0, 0), 0, float3(0, 0, 0), 0 };
	TraceRay(AS, RAY_FLAG_NONE, ~0, 0, 1, 0, dxrRay, payload);

	float2 result;
	result.x = payload.hitT;
	bool hitFound = result.x < maxT;
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
	tangent = payload.tangent;
	return result;
}

void OutputPrimaryAlbedo(float3 albedo)
{
	AOVCustomOutput[DispatchRaysIndex().xy] = float4(albedo, 1.0);
}

void OutputPrimaryNormal(float3 normal)
{
	AOVNormals[DispatchRaysIndex().xy] = AOVNormals[DispatchRaysIndex().xy] + float4(normal, 1.0);
}

void OutputPrimaryWorldPosition(float3 worldPosition, float distanceToNeighbor)
{
	AOVWorldPosition[DispatchRaysIndex().xy] = float4(worldPosition, distanceToNeighbor);
}

void OutputSampleColor(float3 color)
{
	float luma = ColorToLuma(color);
	AOVSummedLumaSquared[DispatchRaysIndex().xy] = AOVSummedLumaSquared[DispatchRaysIndex().xy] + float4(luma * luma, 0.0, 0, 0);
}

bool IsFogEnabled()
{
	return false;
	return perFrameConstants.fogScatterDistance > EPSILON;
}

void ClearAOVs()
{
	OutputPrimaryAlbedo(float3(0.0, 0.0, 0.0));
}

#include "GLSLCompat.h"
#include "kernel.glsl"

#define USE_ADAPTIVE_RAY_DISPATCHING 0

[shader("raygeneration")]
void RayGen()
{
	ClearAOVs();

	seed = RandSeedBuffer[DispatchRaysIndex().x + DispatchRaysIndex().y * DispatchRaysDimensions().x];

#if USE_ADAPTIVE_RAY_DISPATCHING
	float luminanceVariance = min(LuminanceVariance[DispatchRaysIndex().xy].x, LuminanceVariance[DispatchRaysIndex().xy].b);
	luminanceVariance *= perFrameConstants.VarianceMultplier;
	luminanceVariance = WaveActiveMax(lerp(1.0, luminanceVariance, clamp(float(perFrameConstants.GlobalFrameCount) / float(perFrameConstants.SamplesToTarget), 0, 1)));
	// WaveReadLaneFirst causes a state object crash so don't use that
	bool SkipRay = /*WaveReadLaneFirst*/( perFrameConstants.GlobalFrameCount > 2 && rand() > max(luminanceVariance, 0.0));
	SkipRay = WaveActiveAnyTrue(WaveIsFirstLane() && SkipRay);
	AOVCustomOutput[DispatchRaysIndex().xy] = (SkipRay ? vec4(1, 1, 1, 1) : vec4(1, 0.2, 0.2, 1)) * (LastFrameTexture[DispatchRaysIndex().xy] / LastFrameTexture[DispatchRaysIndex().xy].w);
	
	if (SkipRay)
	{
		//OutputTexture[DispatchRaysIndex().xy] = LastFrameTexture[DispatchRaysIndex().xy];
		return;
	}
#endif

	float2 dispatchUV = float2(DispatchRaysIndex().xy + 0.5) / float2(DispatchRaysDimensions().xy);
	float2 uv = vec2(0, 1) + dispatchUV * vec2(1, -1);
	float4 outputColor = PathTrace(uv * GetResolution().xy);
	OutputTexture[DispatchRaysIndex().xy] = outputColor;


}
