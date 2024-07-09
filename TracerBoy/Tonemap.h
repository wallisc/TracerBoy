#pragma once

#define TONEMAP_TYPE_REINHARD 0
#define TONEMAP_TYPE_ACES 1
#define TONEMAP_TYPE_CLAMP 2

float3 ACESFilm(float3 x)
{
	float a = 2.51f;
	float b = 0.03f;
	float c = 2.43f;
	float d = 0.59f;
	float e = 0.14f;
	return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 Reinhard(float3 color)
{
	// Reinhard tonemapping
	return color / (1.0 + color);
}

float3 Tonemap(uint TonemapType, float3 color)
{
	switch (TonemapType)
	{
	case TONEMAP_TYPE_REINHARD:
		return Reinhard(color);
	case TONEMAP_TYPE_ACES:
		return ACESFilm(color);
	default:
	case TONEMAP_TYPE_CLAMP:
		return saturate(color);
	}
}

float ColorToLuma(float3 color)
{
	return dot(color, float3(0.212671, 0.715160, 0.072169));        // Defined by sRGB/Rec.709 gamut
}

float3 GammaToLinear(float3 color)
{
	return pow(color, 2.2f);
}
