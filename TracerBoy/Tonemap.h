#pragma once
float3 Tonemap(float3 color)
{
	// Reinhard tonemapping
	return color / (1.0 + color);
}

float SDRToLuma(float3 color)
{
	return dot(color, float3(0.212671, 0.715160, 0.072169));        // Defined by sRGB/Rec.709 gamut
}

float HDRToLuma(float3 color)
{
	return SDRToLuma(Tonemap(color));
}
