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

// Taken from https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1
// Samples a texture with Catmull-Rom filtering, using 9 texture fetches instead of 16.
// See http://vec3.ca/bicubic-filtering-in-fewer-taps/ for more details
float4 SampleTextureCatmullRom(in Texture2D<float4> tex, in SamplerState linearSampler, in float2 uv, in float2 texSize)
{
    // We're going to sample a a 4x4 grid of texels surrounding the target UV coordinate. We'll do this by rounding
    // down the sample location to get the exact center of our "starting" texel. The starting texel will be at
    // location [1, 1] in the grid, where [0, 0] is the top left corner.
    float2 samplePos = uv * texSize;
    float2 texPos1 = floor(samplePos - 0.5f) + 0.5f;

    // Compute the fractional offset from our starting texel to our original sample location, which we'll
    // feed into the Catmull-Rom spline function to get our filter weights.
    float2 f = samplePos - texPos1;

    // Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
    // These equations are pre-expanded based on our knowledge of where the texels will be located,
    // which lets us avoid having to evaluate a piece-wise function.
    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);

    // Work out weighting factors and sampling offsets that will let us use bilinear filtering to
    // simultaneously evaluate the middle 2 samples from the 4x4 grid.
    float2 w12 = w1 + w2;
    float2 offset12 = w2 / (w1 + w2);

    // Compute the final UV coordinates we'll use for sampling the texture
    float2 texPos0 = texPos1 - 1;
    float2 texPos3 = texPos1 + 2;
    float2 texPos12 = texPos1 + offset12;

    texPos0 /= texSize;
    texPos3 /= texSize;
    texPos12 /= texSize;

    float4 result = 0.0f;
    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos0.y), 0.0f) * w0.x * w0.y;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos0.y), 0.0f) * w12.x * w0.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos0.y), 0.0f) * w3.x * w0.y;

    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos12.y), 0.0f) * w0.x * w12.y;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos12.y), 0.0f) * w12.x * w12.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos12.y), 0.0f) * w3.x * w12.y;

    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos3.y), 0.0f) * w0.x * w3.y;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos3.y), 0.0f) * w12.x * w3.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos3.y), 0.0f) * w3.x * w3.y;

    return result;
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
			const bool bUseCatmullRomSampling = true;
			if (bUseCatmullRomSampling)
			{
				PrevFrameColor = SampleTextureCatmullRom(TemporalHistory, BilinearSampler, UV, Constants.Resolution);
			}
			else
			{
				PrevFrameColor = TemporalHistory.SampleLevel(BilinearSampler, UV, 0).rgb;
			}

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
		float sampleCount = PrevMomentData.b + 1.0;

		float lerpFactor = 1.0 / min(sampleCount, 32.0);
		float2 luminanceDataPair = lerp(PrevMomentData.rg, float2(luminance, luminanceSquared), lerpFactor);
		OutputMoment[DTid.xy] = float3(luminanceDataPair, sampleCount);

		float variance = max(luminanceDataPair.g - luminanceDataPair.r * luminanceDataPair.r, 0.0);
		outputAlpha = variance;
	}
	float3 OutputColor = lerp(RawOutputColor, PrevFrameColor, Constants.HistoryWeight);

	OutputTexture[DTid.xy] = float4(OutputColor, outputAlpha);
}