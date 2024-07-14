#pragma once

#define TONEMAP_TYPE_REINHARD 0
#define TONEMAP_TYPE_ACES 1
#define TONEMAP_TYPE_CLAMP 2
#define TONEMAP_TYPE_UNCHARTED 3

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

float3 uncharted2_tonemap_partial(float3 x)
{
	float A = 0.15f;
	float B = 0.50f;
	float C = 0.10f;
	float D = 0.20f;
	float E = 0.02f;
	float F = 0.30f;
	return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 uncharted2_filmic(float3 v)
{
	float exposure_bias = 2.0f;
	float3 curr = uncharted2_tonemap_partial(v * exposure_bias);

	float3 W = float3(11.2f, 11.2f, 11.2f);
	float3 white_scale = float3(1, 1, 1) / uncharted2_tonemap_partial(W);
	return curr * white_scale;
}

float3 Tonemap(uint TonemapType, float3 color)
{
	switch (TonemapType)
	{
	case TONEMAP_TYPE_REINHARD:
		return Reinhard(color);
	case TONEMAP_TYPE_ACES:
		return ACESFilm(color);
	case TONEMAP_TYPE_UNCHARTED:
		return uncharted2_filmic(color);
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
