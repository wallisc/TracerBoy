#define HLSL
#include "TemporalAccumulationSharedShaderStructs.h"
#include "Tonemap.h"

Texture2D TemporalHistory : register(t0);
Texture2D CurrentFrame : register(t1);
Texture2D WorldPositionTexture : register(t2);
Texture2D MomentHistory : register(t3);
SamplerState BilinearSampler : register(s0);

RWTexture2D<float4> OutputTexture : register(u0);
RWTexture2D<float3> OutputMoment : register(u1);

cbuffer TemporalAccumulationCB
{
	TemporalAccumulationConstants Constants;
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

[numthreads(TEMPORAL_ACCUMULATION_THREAD_GROUP_WIDTH, TEMPORAL_ACCUMULATION_THREAD_GROUP_HEIGHT, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	if (DTid.x >= Constants.Resolution.x || DTid.y >= Constants.Resolution.y) return;

	float3 WorldPosition = WorldPositionTexture[DTid.xy];
	float aspectRatio = float(Constants.Resolution.x) / float(Constants.Resolution.y);
	float lensHeight = Constants.CameraLensHeight;
	float lensWidth = lensHeight * aspectRatio;

	float3 PrevFrameCameraDir = normalize(Constants.PrevFrameCameraLookAt - Constants.PrevFrameCameraPosition);
	float3 PrevFrameFocalPoint = Constants.PrevFrameCameraPosition - Constants.CameraFocalDistance * PrevFrameCameraDir;
	float3 PrevFrameRayDirection = normalize(WorldPosition - PrevFrameFocalPoint);

	float3 RawOutputColor = CurrentFrame[DTid.xy].rgb;
	float3 PrevFrameColor = RawOutputColor;
	float3 PrevMomentData = float3(0, 0, 0);
	float t = PlaneIntersection(PrevFrameFocalPoint, PrevFrameRayDirection, Constants.PrevFrameCameraPosition, PrevFrameCameraDir);
	if (!Constants.IgnoreHistory && t >= 0)
	{
		PrevFrameColor = TemporalHistory[DTid.xy].rgb;
#if 1
		float3 LensPosition = PrevFrameFocalPoint + PrevFrameRayDirection * t;

		float3 OffsetFromCenter = LensPosition  - Constants.PrevFrameCameraPosition;
		float2 UV = float2(
			dot(OffsetFromCenter, Constants.PrevFrameCameraRight) / (lensWidth / 2.0),
			dot(OffsetFromCenter, Constants.PrevFrameCameraUp)    / (lensHeight / 2.0));
		
		// Convert from (-1, 1) -> (0,1)
		UV = (UV + 1.0) / 2.0;
		UV.y = 1.0 - UV.y;
	
		if (all(UV >= 0.0 && UV <= 1.0))
		{
	#if 0
			float distanceToNeighbor;
			float3 PreviousFrameWorldPosition = GetPreviousFrameWorldPosition(UV, distanceToNeighbor);
			distanceToNeighbor = length(MaxWorldPosition - MinWorldPosition) * 0.035;
			if (length(PreviousFrameWorldPosition - WorldPosition) < distanceToNeighbor)
			{
				previousFrameColor = PreviousFrameOutput.SampleLevel(BilinearSampler, UV, 0);
				bValidHistory = true;
			}
	#endif
			PrevFrameColor = TemporalHistory.SampleLevel(BilinearSampler, UV, 0).rgb;
			if (Constants.OutputMomentInformation)
			{
				PrevMomentData = MomentHistory.SampleLevel(BilinearSampler, UV, 0).rgb;
			}
		}
#endif
	}

	float outputAlpha = 1.0;
	if (Constants.OutputMomentInformation)
	{
		float luminance = ColorToLuma(RawOutputColor);
		float luminanceSquared = luminance * luminance;

		float historyLength = clamp(PrevMomentData.b + 1.0, 32.0, 64.0);
		float2 luminanceDataPair = lerp(PrevMomentData.rg, float2(luminance, luminanceSquared), 1.0 / historyLength);
		OutputMoment[DTid.xy] = float3(luminanceDataPair, historyLength);

		float variance = luminanceDataPair.g - luminanceDataPair.r * luminanceDataPair.r;
		outputAlpha = variance;
	}
	float3 OutputColor = lerp(RawOutputColor, PrevFrameColor, Constants.HistoryWeight);

	OutputTexture[DTid.xy] = float4(OutputColor, outputAlpha);
}