RWTexture2D<float4> AOVNormals : register(u1);
RWTexture2D<float4> AOVWorldPosition : register(u2);
RWTexture2D<float2> AOVSummedVariance : register(u4);

[numthreads(1, 1, 1)]
void main( uint2 DTid : SV_DispatchThreadID )
{
	AOVNormals[DTid] = float4(0.0f, 0.0f, 0.0f, 0.0f);
	AOVWorldPosition[DTid] = float4(0.0f, 0.0f, 0.0f, 0.0f);
	AOVSummedVariance[DTid] = float2(0.0, 0.0f);
}