#define HLSL

#define FSR_EASU_F 1
#include "FidelityFXSuperResolutionSharedShaderStructs.h"

Texture2D InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);
SamplerState BilinearSampler : register(s0);

cbuffer FidelityFXSuperResolutionCB
{
	FSRConstants Constants;
}

#define FSR_EASU_F 1
AF4 FsrEasuRF(AF2 p) { AF4 res = InputTexture.GatherRed(BilinearSampler, p, int2(0, 0)); return res; }
AF4 FsrEasuGF(AF2 p) { AF4 res = InputTexture.GatherGreen(BilinearSampler, p, int2(0, 0)); return res; }
AF4 FsrEasuBF(AF2 p) { AF4 res = InputTexture.GatherBlue(BilinearSampler, p, int2(0, 0)); return res; }


void CurrFilter(int2 pos)
{
	AF3 c;
	FsrEasuF(c, pos, Constants.const0, Constants.const1, Constants.const2, Constants.const3);
	//if (Sample.x == 1)
	//	c *= c;
	OutputTexture[pos] = float4(c, 1);
}

[numthreads(64, 1, 1)]
void main(uint3 LocalThreadId : SV_GroupThreadID, uint3 WorkGroupId : SV_GroupID, uint3 Dtid : SV_DispatchThreadID)
{
	// Do remapping of local xy in workgroup for a more PS-like swizzle pattern.
	AU2 gxy = ARmp8x8(LocalThreadId.x) + AU2(WorkGroupId.x << 4u, WorkGroupId.y << 4u);
	CurrFilter(gxy);
	gxy.x += 8u;
	CurrFilter(gxy);
	gxy.y += 8u;
	CurrFilter(gxy);
	gxy.x -= 8u;
	CurrFilter(gxy);
}