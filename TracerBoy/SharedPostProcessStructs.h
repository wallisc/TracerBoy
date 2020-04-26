#include "SharedShaderStructs.h"

#define OUTPUT_TYPE_LIT 0
#define OUTPUT_TYPE_ALBEDO 1
#define OUTPUT_TYPE_NORMAL 2
#define OUTPUT_TYPE_LUMINANCE 3
#define OUTPUT_TYPE_VARIANCE 4

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