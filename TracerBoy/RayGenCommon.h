#define HLSL
#include "SharedShaderStructs.h"
#include "SharedRaytracing.h"
#include "Tonemap.h"

#define IS_SHADER_TOY 0
#define USE_INLINE_RAYTRACING IS_COMPUTE_SHADER

bool ShouldInvalidateHistory() { return perFrameConstants.InvalidateHistory; }
float3 GetCameraPosition() { return perFrameConstants.CameraPosition; }
float3 GetCameraLookAt() { return perFrameConstants.CameraLookAt; }
float3 GetCameraUp() { return perFrameConstants.CameraUp; }
float3 GetCameraRight() { return perFrameConstants.CameraRight; }
float GetCameraLensHeight() { return configConstants.CameraLensHeight; }
float GetCameraFocalDistance() { return perFrameConstants.FocalDistance; }

float GetRotationFactor() { return 0.5f; }

bool IsTargettingRealTime() { return perFrameConstants.IsRealTime; }

float3 SampleEnvironmentMap(float3 v)
{
	v = mul(v, (configConstants.EnvironmentMapTransform)).xyz;
	float3 viewDir = normalize(v);
	float2 uv;
	float p = atan2(
#if 0
		viewDir.z, 
#else
		viewDir.y, 
#endif
		viewDir.x);
	p = p > 0 ? p : p + 2 * 3.14;
	uv.x = p / (2 * 3.14);
	uv.y = acos(
#if 0
		viewDir.y
#else
		viewDir.z
#endif
	) / (3.14);

	return EnvironmentMap.SampleLevel(BilinearSampler, uv, 0).rgb * configConstants.EnvironmentMapColorScale;
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

#define GLOBAL static
GLOBAL uint2 Resolution;
GLOBAL uint2 DispatchIndex;
float GetTime() { return perFrameConstants.Time; }
float3 GetResolution() { 
#if IS_COMPUTE_SHADER
	return float3(Resolution, 1); 
#else
	return DispatchRaysDimensions();
#endif
}
uint2 GetDispatchIndex() {
#if IS_COMPUTE_SHADER
	return DispatchIndex; 
#else
	return DispatchRaysIndex().xy;
#endif
}
float4 GetMouse() { return float4(0.0, 0.0, 0.0, 0.0); }

BlueNoiseData GetBlueNoise()
{
	BlueNoiseData data;
	if (!perFrameConstants.UseBlueNoise)
	{
		data.PrimaryJitter = float2(rand(), rand());
		data.SecondaryRayDirection = float2(rand(), rand());
		data.AreaLightJitter = float2(rand(), rand());
		data.DOFJitter = float2(rand(), rand());
	}
	else
	{
		data.PrimaryJitter = ApplyLDSToNoise( BlueNoise0Texture[(GetDispatchIndex().xy % 256)].xy);
		data.SecondaryRayDirection = ApplyLDSToNoise( BlueNoise0Texture[(GetDispatchIndex().xy % 256)].zw);
		data.AreaLightJitter = ApplyLDSToNoise( BlueNoise1Texture[(GetDispatchIndex().xy % 256)].xy);
		data.DOFJitter = ApplyLDSToNoise( BlueNoise1Texture[(GetDispatchIndex().xy % 256)].zw);
	}
	return data;
}

float3 GetRandomBarycentric()
{
	float u = rand();
	float v = rand();

	if (u + v > 1.0)
	{
		u = 1.0 - u;
		v = 1.0 - v;
	}
	return float3(u, v, 1.0 - u - v);
}

struct RestirReservoir
{
	uint SelectedIndex;
	float3 SelectedBarycentric;

	float WeightSum;
	uint SampleCount;

	void Init()
	{
		SelectedIndex = 0;
		WeightSum = 0.0f;
		SampleCount = 0;
	}

	void AddSample(uint Index, float3 Barycentric, float Weight)
	{
		WeightSum += Weight;
		SampleCount++;
		if (rand() < Weight / WeightSum)
		{
			SelectedIndex = Index;
			SelectedBarycentric = Barycentric;
		}
	}
};

float GetLightTargetPDF(Light light, float3 barycentric, float3 PositionToLight)
{
	float DistanceToLight = length(light.GetPosition(barycentric) - PositionToLight);
	return (light.SurfaceArea * ColorToLuma(light.LightColor)) / DistanceToLight * DistanceToLight;
}

void GetOneLightSample(in float3 PositionToLight, out float3 LightDirection, out float3 LightColor, out float PDFValue, out float3 LightNormal, out float LightAttenuation)
{
	// Initialize
	LightDirection = LightColor = LightNormal = float3(0, 0, 0);
	LightAttenuation = 0.0f;
	PDFValue = 0.0f;

	const uint lightCount = perFrameConstants.LightCount;
	if (lightCount > 0 && perFrameConstants.EnableNextEventEstimation)
	{
		if (perFrameConstants.EnableSamplingImportanceResampling)
		{
#define MAX_SAMPLE_COUNT 16
			RestirReservoir Reservoir;
			Reservoir.Init();

			uint SamplingIportantanceResamplingLightCount = MAX_SAMPLE_COUNT;

			for (uint i = 0; i < SamplingIportantanceResamplingLightCount; i++)
			{
				uint lightIndex = uint(rand() * float(lightCount));

				Light light = LightList[lightIndex];
				float3 barycentric = GetRandomBarycentric();

				float TargetPDF = GetLightTargetPDF(light, barycentric, PositionToLight);
				float proposalPDF = 1.0 / float(lightCount);

				Reservoir.AddSample(lightIndex, barycentric, TargetPDF / (proposalPDF * SamplingIportantanceResamplingLightCount));
			}

			Light light = LightList[Reservoir.SelectedIndex];
			float3 barycentric = Reservoir.SelectedBarycentric;
			float SamplingImportanceResamplingPDF = GetLightTargetPDF(light, barycentric, PositionToLight) / Reservoir.WeightSum;
			PDFValue = SamplingImportanceResamplingPDF / (light.SurfaceArea);


			float3 LightPosition = light.P0 * barycentric.x + light.P1 * barycentric.y + light.P2 * barycentric.z;
			LightDirection = LightPosition - PositionToLight;
			LightNormal = light.N0 * barycentric.x + light.N1 * barycentric.y + light.N2 * barycentric.z;
			LightColor = light.LightColor;
		}
		else
		{
			uint lightIndex = uint(rand() * float(lightCount));
			Light light = LightList[lightIndex];
			float3 barycentric = GetRandomBarycentric();

			switch (light.LightType)
			{
			case LIGHT_TYPE_AREA:
			{
				float3 LightPosition = light.P0 * barycentric.x + light.P1 * barycentric.y + light.P2 * barycentric.z;
				LightDirection = LightPosition - PositionToLight;

				LightNormal = light.N0 * barycentric.x + light.N1 * barycentric.y + light.N2 * barycentric.z;

				float DistanceToLight = length(LightDirection);
				LightAttenuation = 1.0 / (DistanceToLight * DistanceToLight);
				LightDirection /= DistanceToLight;
				break;
			}
			case LIGHT_TYPE_DIRECTIONAL:
			{
				LightDirection = -light.Direction;

				if (perFrameConstants.DebugValue > 0.0)
				{
					LightDirection.x = sin(perFrameConstants.DebugValue);
					LightDirection.y = sin(perFrameConstants.DebugValue2);
					LightDirection = normalize(LightDirection);
				}

				LightNormal = -LightDirection;
				LightAttenuation = 1.0f;
				break;
			}
			default:
				break;
				// Should never get hit...
			}

			LightColor = light.LightColor;

			PDFValue = 1.0 / (lightCount);
			if (light.LightType == LIGHT_TYPE_AREA)
			{
				PDFValue /= light.SurfaceArea;
			}
		}
	}
}


float4 GetLastFrameData() {
	return OutputTexture[float2(0, GetResolution().y - 1)];
}
float4 GetAccumulatedColor(float2 uv) 
{
	return OutputTexture[GetDispatchIndex().xy];
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


Material GetMaterialInternal(int MaterialID, uint PrimitiveID, float3 WorldPosition, float2 uv, bool IsBacksideOfGeometry)
{
	Material mat = MaterialBuffer[MaterialID];
	// Following PBRT's implementation and choosing not to emit light on the backside of geometry
	bool ShouldIgnoreEmissive = IsBacksideOfGeometry;
	if (ShouldIgnoreEmissive)
	{
		mat.emissive = float3(0, 0, 0);
	}

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

	if (IsValidTexture(mat.emissiveIndex) && !ShouldIgnoreEmissive)
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

#include "SharedHitGroup.h"


void OutputPrimaryAlbedo(float3 albedo, float DiffuseContribution);
void OutputRayStats(uint TrianglesTested, uint BoxesTested);

#if USE_SW_RAYTRACING
#define HLSL
#define DISABLE_ANYHIT
#define DISABLE_PROCEDURAL_GEOMETRY
#define FAST_PATH 1
#include "..\D3D12RaytracingFallback\src\RayTracingHlslCompat.h"
#include "..\D3D12RaytracingFallback\src\TraverseShader.hlsli"
#endif

#define MIN_T 0.001f
float2 IntersectWithMaxDistance(Ray ray, float maxT, out float3 normal, out float3 tangent, out float2 uv, out uint PrimitiveID)
{
	RayDesc dxrRay;
	dxrRay.Origin = ray.origin;
	dxrRay.Direction = ray.direction;
	dxrRay.TMin = MIN_T;
	dxrRay.TMax = maxT;

#if USE_INLINE_RAYTRACING

#if USE_SW_RAYTRACING
	SoftwareRayQuery Query;
	Query.TraceRayInline(
		RAY_FLAG_NONE,
		~0,
		dxrRay,
		GI);

	Query.Proceed();

	float2 result;
	RayPayload payload = { float2(0, 0), uint(-1), maxT, float3(0, 0, 0), 0, float3(0, 0, 0), 0 };
	if (Query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
	{
		result.x = Query.CommittedRayT();

		float3 barycentrics = GetBarycentrics3(Query.CommittedTriangleBarycentrics());
		uint GeometryIndex = Query.CommittedInstanceIndex() + Query.CommittedGeometryIndex();
		GeometryInfo Geometry = GetGeometryInfo(GeometryIndex);
		HitInfo hit = GetHitInfo(Geometry, Query.CommittedPrimitiveIndex(), barycentrics);
		payload.uv = hit.uv;
		payload.materialIndex = Geometry.MaterialIndex;

		payload.hitT = Query.CommittedRayT();
		payload.normal = hit.normal;
		payload.tangent = hit.tangent;

		result.y = payload.materialIndex;
	}
	else
	{
		result.x = -1;
		result.y = -1;
	}
	PrimitiveID = 0;
	normal = payload.normal;
	uv = payload.uv;
	tangent = payload.tangent;

	OutputRayStats(Query.TrianglesTested, Query.BoxesTested);
#else
	RayQuery<RAY_FLAG_NONE> Query;
    Query.TraceRayInline(
        AS,
        RAY_FLAG_NONE,
        ~0,
        dxrRay);

	while (Query.Proceed())
	{
		float3 barycentrics = GetBarycentrics3(Query.CandidateTriangleBarycentrics());
		uint GeometryIndex = Query.CandidateInstanceIndex() + Query.CandidateGeometryIndex();
		GeometryInfo Geometry = GetGeometryInfo(GeometryIndex);
		HitInfo hit = GetHitInfo(Geometry, Query.CandidatePrimitiveIndex(), barycentrics);

		if (IsValidHit(Geometry, hit))
		{
			Query.CommitNonOpaqueTriangleHit();
		}
	}

	float2 result;
	RayPayload payload = { float2(0, 0), uint(-1), maxT, float3(0, 0, 0), 0, float3(0, 0, 0), 0 };
    if(Query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
	{
		result.x = Query.CommittedRayT();

		float3 barycentrics = GetBarycentrics3(Query.CommittedTriangleBarycentrics());
		uint GeometryIndex = Query.CommittedInstanceIndex() + Query.CommittedGeometryIndex();
		GeometryInfo Geometry = GetGeometryInfo(GeometryIndex);
		HitInfo hit = GetHitInfo(Geometry, Query.CommittedPrimitiveIndex(), barycentrics);
		payload.uv = hit.uv;
		payload.materialIndex = Geometry.MaterialIndex;
		payload.hitT = Query.CommittedRayT();
		payload.normal = hit.normal;
		payload.tangent = hit.tangent;

		result.y = payload.materialIndex;
	}
	else
	{
		result.x = -1;
		result.y = -1;
	}
	PrimitiveID = 0;
	normal = payload.normal;
	uv = payload.uv;
	tangent = payload.tangent;
#endif

#else
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
#endif

	PrimitiveID = 0;
	normal = payload.normal;
	uv = payload.uv;
	tangent = payload.tangent;
	return result;
}

float2 IntersectAnything(Ray ray, float maxT, out float3 normal, out float3 tangent, out float2 uv, out uint PrimitiveID)
{
#if IS_COMPUTE_SHADER
	//TODO!!
	return float2(0, 0);
#else
	return float2(0, 0);
	RayDesc dxrRay;
	dxrRay.Origin = ray.origin;
	dxrRay.Direction = ray.direction;
	dxrRay.TMin = MIN_T;
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
#endif
}

void OutputPrimaryAlbedo(float3 albedo, float DiffuseContribution)
{
	//if (perFrameConstants.OutputMode == OUTPUT_TYPE_ALBEDO)
	{
		AOVCustomOutput[GetDispatchIndex().xy] = float4(albedo, 1.0);
	}
}

void OutputPrimaryEmissive(float3 emissive)
{
	AOVEmissive[GetDispatchIndex().xy] = float4(emissive, 1.0);
}

void OutputRayStats(uint TrianglesTested, uint BoxesTested)
{
	if (perFrameConstants.OutputMode == OUTPUT_TYPE_HEATMAP)
	{
		AOVCustomOutput[GetDispatchIndex().xy] = float4(TrianglesTested, BoxesTested, 0, 0);
	}
}

void OutputLivePixels(bool bAlive)
{
	if (perFrameConstants.OutputMode == OUTPUT_TYPE_LIVE_PIXELS)
	{
		AOVCustomOutput[GetDispatchIndex().xy] = (bAlive ? float4(1, 1, 1, 1) : float4(1, 0.2, 0.2, 1)) * (OutputTexture[GetDispatchIndex().xy] / OutputTexture[GetDispatchIndex().xy].w);
	}
}

void OutputLiveWaves(uint GroupID)
{
	if (perFrameConstants.OutputMode == OUTPUT_TYPE_LIVE_WAVES)
	{
		float groupRand = frac(sin(GroupID) * 43758.5453123);
		float3 filter = lerp(float3(1, 0, 1), float3(1, 0, 0), groupRand);
		if (groupRand < 0.33)
		{
			filter = lerp(float3(1, 0, 0), float3(0, 0, 1), (groupRand) / 0.33);
		}
		else if(groupRand < 0.66)
		{
			filter = lerp(float3(1, 0, 1), float3(1, 1, 1), (groupRand - 0.33) / 0.33);
		}
		else
		{
			filter = lerp(float3(1, 1, 0), float3(0, 1, 0), (groupRand - 0.66) / 0.33);
		}
		AOVCustomOutput[GetDispatchIndex().xy] = float4(filter, perFrameConstants.GlobalFrameCount);
	}
}

void OutputPrimaryNormal(float3 normal)
{
	AOVNormals[GetDispatchIndex().xy] = float4(normal, 1.0);
}

bool OutputWorldPositionToCustomOutput()
{
	return false;
}

static float3 WorldPosition;
static float DistanceToNeighbor;
void OutputPrimaryWorldPosition(float3 worldPosition, float distanceToNeighbor)
{
	WorldPosition += worldPosition;
	DistanceToNeighbor += distanceToNeighbor;
}

bool IsSelectedPixel()
{
	return all(GetDispatchIndex() == uint2(perFrameConstants.SelectedPixelX, perFrameConstants.SelectedPixelY));
}

#include "VisualizationRaysCommon.h"

int RequestVisualizationRayIndex()
{
	int AllocatedIndex = -1;
	VisualizationRaysBuffer.InterlockedAdd(0, 1, AllocatedIndex);
	bool bOutOfSpace = AllocatedIndex >= MAX_VISUALIZER_RAYS;
	if (bOutOfSpace)
	{
		AllocatedIndex = -1;
	}

	return AllocatedIndex;
}

bool IsValidVisualizationRayIndex(int VisualizationRayIndex)
{
	return VisualizationRayIndex >= 0;
}

void OutputVisualizationRay(float3 Origin, float3 Direction, float HitT, float BounceCount)
{
	if (IsSelectedPixel())
	{
		int VisualizationRayIndex = RequestVisualizationRayIndex();
		if (IsValidVisualizationRayIndex(VisualizationRayIndex))
		{
			int OffsetToVisualizationRay = GetOffsetToVisualizationRay(VisualizationRayIndex);
			VisualizationRaysBuffer.Store4(OffsetToVisualizationRay + 0, asuint(float4(Origin, Direction.x)));
			VisualizationRaysBuffer.Store4(OffsetToVisualizationRay + 16, asuint(float4(Direction.yz, HitT, BounceCount)));
		}
	}
}

void OutputDistanceToFirstHit(float Distance)
{
	AOVDepth[GetDispatchIndex().xy] = saturate(Distance / perFrameConstants.MaxZ);

	if (IsSelectedPixel())
	{
		StatsBuffer.Store(8, asuint(Distance));
	}
}

void OutputMaterial(int MaterialID)
{
	if (IsSelectedPixel())
	{
		StatsBuffer.Store(12, MaterialID);
	}
}

void ClearAOVs()
{
	OutputPrimaryAlbedo(float3(0.0, 0.0, 0.0), 1.0);
	OutputPrimaryNormal(float3(0.0, 0.0, 0.0));
}

#include "GLSLCompat.h"
#include "kernel.glsl"
#include "VarianceUtil.h"

#define USE_ADAPTIVE_RAY_DISPATCHING 0

float hash13(vec3 p3)
{
	p3  = fract(p3 * .1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

bool ShouldSkipRay()
{
	return ShouldSkipRay(
		OutputTexture,
		JitteredOutputTexture,
		GetDispatchIndex().xy,
		perFrameConstants.MinConvergence,
		perFrameConstants.GlobalFrameCount);
}

float PlaneIntersection(float3 RayOrigin, float3 RayDirection, float3 PlaneOrigin, float3 PlaneNormal)
{
      float denom = dot(PlaneNormal, RayDirection);
      if(abs(denom) > 0.0)
      {
          float t =  dot(PlaneOrigin - RayOrigin, PlaneNormal) / denom;
		return t;
      }
      return -1.0;
}

void RayTraceCommon()
{
	const float BigNumber = 99999999.0f;
	WorldPosition = float3(0, 0, 0);
	DistanceToNeighbor = 0.0;
	 
	float2 dispatchUV = float2(GetDispatchIndex().xy + 0.5) / float2(GetResolution().xy);
	float2 uv = vec2(0, 1) + dispatchUV * vec2(1, -1);

	const uint NumSamples = 1;
	float4 outputColor = float4(0, 0, 0, 0); 
	for (uint i = 0; i < NumSamples; i++)
	{
		float4 color = PathTrace(uv * GetResolution().xy);
		if (all(!isnan(color)))
		{
			outputColor += color;
		}
	}
	outputColor /= NumSamples;
	WorldPosition /= NumSamples;
	DistanceToNeighbor /= NumSamples;
	if ((perFrameConstants.GlobalFrameCount % 2) == 0)
	{
		AOVWorldPosition0[GetDispatchIndex().xy] = float4(WorldPosition, DistanceToNeighbor);
	}
	else
	{
		AOVWorldPosition1[GetDispatchIndex().xy] = float4(WorldPosition, DistanceToNeighbor);
	}

	float4 accumulatedColor = perFrameConstants.IsRealTime || perFrameConstants.GlobalFrameCount == 0 ? float4(0, 0, 0, 0) : OutputTexture[GetDispatchIndex().xy];
	OutputTexture[GetDispatchIndex().xy] = outputColor + accumulatedColor;
	if (!perFrameConstants.IsRealTime && (perFrameConstants.GlobalFrameCount == 0 || rand() < 0.5))
	{
		float4 accumulatedColor = perFrameConstants.GlobalFrameCount > 0 ? JitteredOutputTexture[GetDispatchIndex().xy] : float4(0, 0, 0, 0);
		JitteredOutputTexture[GetDispatchIndex().xy] = outputColor + accumulatedColor;
	}
}