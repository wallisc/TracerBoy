#define HLSL
#include "Tonemap.h"
#include "SharedPostProcessStructs.h"

cbuffer RootConstants
{
	PostProcessConstants Constants;
}

Texture2D InputTexture;
RWTexture2D<float4> OutputTexture;

float3 GammaCorrect(float3 color)
{
	return pow(color, float4(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2, 1));

}

#define ComputeRS \
    "RootConstants(num32BitConstants=4, b0),\
    DescriptorTable(UAV(u0, numDescriptors=1), visibility=SHADER_VISIBILITY_ALL),\
    DescriptorTable(SRV(t0, numDescriptors=1), visibility=SHADER_VISIBILITY_ALL)"

float3 ProcessLit(float4 color)
{
	float FrameCount = InputTexture[float2(0, Constants.Resolution.y - 1)].x;
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
	float FrameCount = InputTexture[float2(0, Constants.Resolution.y - 1)].x;
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
	uint frameCount = color.g;
	return frameCount > 0 ? Constants.VarianceMultiplier * float3(color.r / frameCount, 0, 0) : float3(0, 0, 0);
}

[RootSignature(ComputeRS)]
[numthreads(1, 1, 1)]
void main( uint2 DTid : SV_DispatchThreadID )
{
	if (DTid.x >= Constants.Resolution.x || DTid.y >= Constants.Resolution.y) return;

	float4 colorData = InputTexture[DTid.xy];
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
	case OUTPUT_TYPE_LUMINANCE:
		outputColor = ProcessLuminance(colorData);
		break;
	case OUTPUT_TYPE_VARIANCE:
		outputColor = ProcessLuminanceVariance(colorData);
		break;
	}

	OutputTexture[DTid] = float4(outputColor, 1);
}