#pragma once

#define TONEMAP_TYPE_REINHARD 0
#define TONEMAP_TYPE_ACES 1
#define TONEMAP_TYPE_CLAMP 2
#define TONEMAP_TYPE_UNCHARTED 3
#define TONEMAP_TYPE_KHRONOS_PBR_NEUTRAL 4
#define TONEMAP_TYPE_AGX 5
#define TONEMAP_TYPE_AGX_PUNCHY 6
#define TONEMAP_TYPE_GT 7

float ColorToLuma(float3 color)
{
	return dot(color, float3(0.212671, 0.715160, 0.072169));        // Defined by sRGB/Rec.709 gamut
}

// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
static const float3x3 ACESInputMat =
{
	{0.59719, 0.35458, 0.04823},
	{0.07600, 0.90834, 0.01566},
	{0.02840, 0.13383, 0.83777}
};

// ODT_SAT => XYZ => D60_2_D65 => sRGB
static const float3x3 ACESOutputMat =
{
	{ 1.60475, -0.53108, -0.07367},
	{-0.10208,  1.10813, -0.00605},
	{-0.00327, -0.07276,  1.07602}
};

float3 RRTAndODTFit(float3 v)
{
	float3 a = v * (v + 0.0245786f) - 0.000090537f;
	float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
	return a / b;
}

float3 ACESFitted(float3 color)
{
	color = mul(ACESInputMat, color);

	// Apply RRT and ODT
	color = RRTAndODTFit(color);

	color = mul(ACESOutputMat, color);

	// Clamp to [0, 1]
	color = saturate(color);

	return color;
}

float3 Reinhard(float3 color)
{
	// Reinhard tonemapping
	return color / (1.0 + color);
}

// https://64.github.io/tonemapping/
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

// https://64.github.io/tonemapping/
float3 uncharted2_filmic(float3 v)
{
	float exposure_bias = 2.0f;
	float3 curr = uncharted2_tonemap_partial(v * exposure_bias);

	float3 W = float3(11.2f, 11.2f, 11.2f);
	float3 white_scale = float3(1, 1, 1) / uncharted2_tonemap_partial(W);
	return curr * white_scale;
}

// https://modelviewer.dev/examples/tone-mapping
float3 CommerceToneMapping(float3 color) {
	float startCompression = 0.8 - 0.04;
	float desaturation = 0.15;

	float x = min(color.r, min(color.g, color.b));
	float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
	color -= offset;

	float peak = max(color.r, max(color.g, color.b));
	if (peak < startCompression) return color;

	float d = 1. - startCompression;
	float newPeak = 1. - d * d / (peak + d - startCompression);
	color *= newPeak / peak;

	float g = 1. - 1. / (desaturation * (peak - newPeak) + 1.);
	return lerp(color, newPeak * float3(1, 1, 1), g);
}

float3 agxDefaultContrastApproximation(float3 x) {
	float3 x2 = x * x;
	float3 x4 = x2 * x2;
	return +15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

float3 agx(float3 color) {

	const float3x3 agxTransform = float3x3(
		0.842479062253094, 0.0423282422610123, 0.0423756549057051,
		0.0784335999999992, 0.878468636469772, 0.0784336,
		0.0792237451477643, 0.0791661274605434, 0.879142973793104);

	const float minEv = -12.47393;
	const float maxEv = 4.026069;
	color = mul(color, agxTransform);
	color = clamp(log2(color), minEv, maxEv);
	color = (color - minEv) / (maxEv - minEv);
	return agxDefaultContrastApproximation(color);
}


float3 agxLook(float3 val, bool punchy) {
	const float3 lw = float3(0.2126, 0.7152, 0.0722);
	float luma = dot(val, lw);

	float3 offset = float3(0, 0, 0);

	// Default
	float3 slope = float3(1, 1, 1);
	float3 power = float3(1, 1, 1);
	float sat = 1.0;

	if (punchy)
	{
		slope = float3(1, 1, 1);
		power = float3(1.35, 1.35, 1.35);
		sat = 1.4;
	}

	// ASC CDL
	val = pow(val * slope + offset, power);
	return luma + sat * (val - luma);
}

float3 GammaCorrect(float3 color)
{
	return pow(color, float4(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2, 1));
}

float GTTonemap(float x) {
	float m = 0.22; // linear section start
	float a = 1.0;  // contrast
	float c = 1.33; // black brightness
	float P = 1.0;  // maximum brightness
	float l = 0.4;  // linear section length
	float l0 = ((P - m) * l) / a; // 0.312
	float S0 = m + l0; // 0.532
	float S1 = m + a * l0; // 0.532
	float C2 = (a * P) / (P - S1); // 2.13675213675
	float L = m + a * (x - m);
	float T = m * pow(x / m, c);
	float S = P - (P - S1) * exp(-C2 * (x - S0) / P);
	float w0 = 1 - smoothstep(0.0, m, x);
	float w2 = (x < m + l) ? 0 : 1;
	float w1 = 1 - w0 - w2;
	return float(T * w0 + L * w1 + S * w2);
}

float3 Tonemap(uint TonemapType, float3 color)
{
	switch (TonemapType)
	{
	case TONEMAP_TYPE_REINHARD:
		color = Reinhard(color);
		return GammaCorrect(color);
	case TONEMAP_TYPE_GT:
		color = float3(
			GTTonemap(color.r),
			GTTonemap(color.g),
			GTTonemap(color.b));
		return GammaCorrect(color);
	case TONEMAP_TYPE_ACES:
		color = ACESFitted(color);
		return GammaCorrect(color);
	case TONEMAP_TYPE_UNCHARTED:
		color = uncharted2_filmic(color);
		return GammaCorrect(color);
	case TONEMAP_TYPE_KHRONOS_PBR_NEUTRAL:
		color = CommerceToneMapping(color);
		return GammaCorrect(color);
	case TONEMAP_TYPE_AGX:
		return agxLook(agx(color), false);
	case TONEMAP_TYPE_AGX_PUNCHY:
		return agxLook(agx(color), true);
	default:
	case TONEMAP_TYPE_CLAMP:
		color = saturate(color);
		return GammaCorrect(color);
	}
}



float3 GammaToLinear(float3 color)
{
	return pow(color, 2.2f);
}
