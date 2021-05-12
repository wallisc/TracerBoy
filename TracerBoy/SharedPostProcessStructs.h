#include "SharedShaderStructs.h"

struct PostProcessConstants
{
	uint2 Resolution;
	uint FramesRendered;
	float ExposureMultiplier;
	uint UseToneMapping;
	uint UseGammaCorrection;
	uint OutputType;
	float VarianceMultiplier;
};