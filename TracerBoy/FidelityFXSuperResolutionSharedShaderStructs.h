#pragma once

#include "SharedShaderStructs.h"
#ifdef HLSL
#define A_GPU 1
#define A_HLSL 1
#else
#define A_CPU
#endif

#include "ffx_a.h"
#include "ffx_fsr1.h"

#ifdef HLSL
struct FSRConstants
{
	uint4 const0;
	uint4 const1;
	uint4 const2;
	uint4 const3;
};
#else
struct FSRConstants
{
	AU1 const0[4];
	AU1 const1[4];
	AU1 const2[4];
	AU1 const3[4];
};
#endif

