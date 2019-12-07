cbuffer RootConstants
{
	uint2 Resolution;
	uint FramesRendered;
}

Texture2D InputTexture;
RWTexture2D<float4> OutputTexture;

[numthreads(1, 1, 1)]
void main( uint2 DTid : SV_DispatchThreadID )
{
	if (DTid.x >= Resolution.x || DTid.y >= Resolution.y) return;

	float FrameCount = InputTexture[float2(0, Resolution.y - 1)].x;
	float4 outputColor = InputTexture[DTid] / FrameCount;

	// Gamma Correct
	outputColor = pow(outputColor, float4(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2, 1));

	OutputTexture[DTid] = outputColor;
}