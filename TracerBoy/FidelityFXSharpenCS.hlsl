#define HLSL

#define FSR_RCAS_F
#include "FidelityFXSuperResolutionSharedShaderStructs.h"

Texture2D InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);
SamplerState BilinearSampler : register(s0);

cbuffer FidelityFXSuperResolutionCB
{
	FSRConstants Constants;
}

AF4 FsrRcasLoadF(ASU2 p) { return InputTexture.Load(int3(ASU2(p), 0)); }
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}

void CurrFilter(int2 pos)
{
	AF3 c;
	FsrRcasF(c.r, c.g, c.b, pos, Constants.const0);
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