#define HLSL
#include "SharedShaderStructs.h"
#include "SharedRaytracing.h"
#include "Tonemap.h"

#define IS_SHADER_TOY 0
#define SUPPORT_VOLUMES 0

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

	return 0.3 * EnvironmentMap.SampleLevel(BilinearSampler, uv, 0).rgb;
}

float rand();

// halton low discrepancy sequence, from https://www.shadertoy.com/view/tl2GDw
float Halton(int b, int i)
{
    float r = 0.0;
    float f = 1.0;
    while (i > 0) {
        f = f / float(b);
        r = r + f * float(i % b);
        i = int(floor(float(i) / float(b)));
    }
    return r;
}

float Halton2(int i)
{
	return Halton(2, i);
}

float2 Halton23(int i)
{
    return float2(Halton2(i), Halton(3, i));
}

struct BlueNoiseData
{
	float2 PrimaryJitter;
	float2 SecondaryRayDirection;
	float2 AreaLightJitter;
	float2 DOFJitter;
};

float2 ApplyLDSToNoise(float2 Noise)
{
	return frac(Noise + Halton23(perFrameConstants.GlobalFrameCount));
}

BlueNoiseData GetBlueNoise()
{
	BlueNoiseData data;
	if (!perFrameConstants.UseBlueNoise || DispatchRaysIndex().x > 400)
	{
		data.PrimaryJitter = float2(rand(), rand());
		data.SecondaryRayDirection = float2(rand(), rand());
		data.AreaLightJitter = float2(rand(), rand());
		data.DOFJitter = float2(rand(), rand());
	}
	else
	{
		data.PrimaryJitter = ApplyLDSToNoise( BlueNoise0Texture[(DispatchRaysIndex().xy % 256)].xy);
		data.SecondaryRayDirection = ApplyLDSToNoise( BlueNoise0Texture[(DispatchRaysIndex().xy % 256)].zw);
		data.AreaLightJitter = ApplyLDSToNoise( BlueNoise1Texture[(DispatchRaysIndex().xy % 256)].xy);
		data.DOFJitter = ApplyLDSToNoise( BlueNoise1Texture[(DispatchRaysIndex().xy % 256)].zw);
	}

	return data;
}

void GetOneLightSample(out float3 LightPosition, out float3 LightColor, out float PDFValue)
{
	LightPosition = float3(0.172, -0.818, -0.549) * -1000.0f;
	BlueNoiseData BlueNoise = GetBlueNoise();
	LightPosition.xz += float2(BlueNoise.AreaLightJitter.x * 2.0 - 1.0, BlueNoise.AreaLightJitter.y * 2.0 - 1.0) * 100.0f;

	LightColor = float3(1.0, 1.0, 1.0) * 1;
	PDFValue = 1.0;
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

float2 IntersectAnything(Ray ray, float maxT, out float3 normal, out float3 tangent, out float2 uv, out uint PrimitiveID)
{
	RayDesc dxrRay;
	dxrRay.Origin = ray.origin;
	dxrRay.Direction = ray.direction;
	dxrRay.TMin = 0.001;
	dxrRay.TMax = maxT;

	RayPayload payload = { float2(0, 0), uint(-1), maxT, float3(0, 0, 0), 0, float3(0, 0, 0), 0 };
	TraceRay(AS, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, ~0, 0, 1, 0, dxrRay, payload);

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
	//AOVCustomOutput[DispatchRaysIndex().xy] = float4(albedo, 1.0);
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

#define USE_ADAPTIVE_RAY_DISPATCHING 1

float hash13(vec3 p3)
{
	p3  = fract(p3 * .1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

[shader("raygeneration")]
void RayGen()
{
	ClearAOVs();

	seed = hash13(float3(DispatchRaysIndex().x, DispatchRaysIndex().y, perFrameConstants.GlobalFrameCount));

#if USE_ADAPTIVE_RAY_DISPATCHING
	bool SkipRay = false;
	if(perFrameConstants.GlobalFrameCount > 16)
	{
		float4 jitteredOutput = JitteredOutputTexture[DispatchRaysIndex().xy];
		float4 output = OutputTexture[DispatchRaysIndex().xy];
		float3 jitteredColor = jitteredOutput.rgb / jitteredOutput.a;
		float3 color = output.rgb / output.a;
		
		float error = (abs(jitteredColor.r - color.r) + abs(jitteredColor.g - color.g) + abs(jitteredColor.b - color.b)) / sqrt(color.r + color.g + color.b);
		//error = WaveActiveSum(error) / WaveActiveSum(1.0);
		SkipRay = error < perFrameConstants.MinConvergence;
	}
	AOVCustomOutput[DispatchRaysIndex().xy] = (SkipRay ? vec4(1, 1, 1, 1) : vec4(1, 0.2, 0.2, 1)) * (OutputTexture[DispatchRaysIndex().xy] / OutputTexture[DispatchRaysIndex().xy].w);
	
	if (SkipRay) return;
#endif

	float2 dispatchUV = float2(DispatchRaysIndex().xy + 0.5) / float2(DispatchRaysDimensions().xy);
	float2 uv = vec2(0, 1) + dispatchUV * vec2(1, -1);
	float4 outputColor = PathTrace(uv * GetResolution().xy);
	float4 accumulatedColor = perFrameConstants.GlobalFrameCount > 0 ? OutputTexture[DispatchRaysIndex().xy] : float4(0, 0, 0, 0);
	OutputTexture[DispatchRaysIndex().xy] = outputColor + accumulatedColor;
	if (perFrameConstants.GlobalFrameCount == 0 || rand() < 0.5)
	{
		float4 accumulatedColor = perFrameConstants.GlobalFrameCount > 0 ? JitteredOutputTexture[DispatchRaysIndex().xy] : float4(0, 0, 0, 0);
		JitteredOutputTexture[DispatchRaysIndex().xy] = outputColor + accumulatedColor;
	}


}
