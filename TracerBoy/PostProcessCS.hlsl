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
	float FrameCount = InputTexture[float2(0, Resolution.y - 1)].x;
	if (DTid.x < Resolution.x && DTid.y < Resolution.y)
	{
		OutputTexture[DTid] = InputTexture[DTid] / FrameCount;
	}
}