#define HLSL
#include "Tonemap.h"
#include "SharedPostProcessStructs.h"

cbuffer RootConstants
{
	PostProcessConstants Constants;
}

Texture2D InputTexture : register(t0);
Texture2D AuxilaryTexture: register(t1);
RWTexture2D<float4> OutputTexture;

float3 GammaCorrect(float3 color)
{
	return pow(color, float4(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2, 1));

}

#define ComputeRS \
    "DescriptorTable(UAV(u0, numDescriptors=1), visibility=SHADER_VISIBILITY_ALL),\
    DescriptorTable(SRV(t0, numDescriptors=1), visibility=SHADER_VISIBILITY_ALL),\
    DescriptorTable(SRV(t1, numDescriptors=1), visibility=SHADER_VISIBILITY_ALL),\
    RootConstants(num32BitConstants=8, b0)"

float3 ProcessLit(float4 color)
{
	float FrameCount = color.w;
	float3 outputColor = color / FrameCount;

	if (Constants.UseToneMapping)
	{
		outputColor *= Constants.ExposureMultiplier;
		outputColor = Tonemap(outputColor);
	}

	if (Constants.UseGammaCorrection)
	{
		outputColor = GammaCorrect(outputColor);
	}
	return outputColor;
}

float3 ProcessLuminance(float4 color) {
	float FrameCount = color.w;
	float3 outputColor = color / FrameCount;

	if (Constants.UseToneMapping)
	{
		outputColor *= Constants.ExposureMultiplier;
		outputColor = Tonemap(outputColor);
	}

	outputColor = ColorToLuma(outputColor);

	if (Constants.UseGammaCorrection)
	{
		outputColor = GammaCorrect(outputColor);
	}
	return outputColor;
}

float3 ProcessAlbedo(float4 color)
{
	float3 outputColor = color;
	if (Constants.UseToneMapping)
	{
		outputColor *= Constants.ExposureMultiplier;
		outputColor = Tonemap(outputColor);
	}

	if (Constants.UseGammaCorrection)
	{
		outputColor = GammaCorrect(outputColor);
	}
	return outputColor;
}

float3 ProcessNormal(float4 color)
{
	uint frameCount = color.w;
	float3 summedNormals = color.xyz;
	return frameCount > 0 ? normalize(summedNormals / frameCount) : float3(0, 0, 0);
}

float3 ProcessLuminanceVariance(float4 color)
{
	return Constants.VarianceMultiplier * float3(color.r , 0, 0);
}

float3 ProcessLiveWaves(float4 color, float4 filter)
{
	float3 output = ProcessLit(color);
	if (any(filter > 0.1))
	{
		output = filter;
	}
	return output;
}

float3 ProcessMotionVectors(float4 color)
{
	float3 output = float3(color.xy / float2(Constants.Resolution), 0.0f);
	if (Constants.UseGammaCorrection)
	{
		output = GammaCorrect(output);
	}

	return output;
}

float3 PassThroughColor(float4 color)
{
	float3 outputColor = color;
	if (Constants.UseToneMapping)
	{
		outputColor *= Constants.ExposureMultiplier;
		outputColor = Tonemap(outputColor);
	}

	if (Constants.UseGammaCorrection)
	{
		outputColor = GammaCorrect(outputColor);
	}
	return outputColor;
}

[RootSignature(ComputeRS)]
[numthreads(8, 8, 1)]
void main( uint2 DTid : SV_DispatchThreadID )
{
	if (DTid.x >= Constants.Resolution.x || DTid.y >= Constants.Resolution.y) return;

	const bool bFlipImageHorizontally = false;
	uint2 InputIndex = bFlipImageHorizontally ? uint2(Constants.Resolution.x - DTid.x - 1, DTid.y) : DTid;

	float4 colorData = InputTexture[InputIndex];
	float4 auxData = AuxilaryTexture[InputIndex];

	float3 outputColor;
	switch (Constants.OutputType)
	{
	case OUTPUT_TYPE_LIT:
	default:
		outputColor = ProcessLit(colorData);
		break;
	case OUTPUT_TYPE_ALBEDO:
		outputColor = ProcessAlbedo(colorData);
		break;
	case OUTPUT_TYPE_NORMAL:
		outputColor = ProcessNormal(colorData);
		break;
	case OUTPUT_TYPE_DEPTH:
		outputColor = PassThroughColor(colorData);
		break;
	case OUTPUT_TYPE_MOTION_VECTORS:
		outputColor = ProcessMotionVectors(colorData);
		break;
	case OUTPUT_TYPE_LUMINANCE:
		outputColor = ProcessLuminance(colorData);
		break;
	case OUTPUT_TYPE_VARIANCE:
		outputColor = ProcessLuminanceVariance(colorData);
		break;
	case OUTPUT_TYPE_LIVE_PIXELS:
		outputColor = PassThroughColor(colorData);
		break;
	case OUTPUT_TYPE_LIVE_WAVES:
		outputColor = ProcessLiveWaves(colorData, auxData);
		break;
	}

	OutputTexture[DTid] = float4(outputColor, 1);
}