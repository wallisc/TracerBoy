RWTexture2D<float4> AOVNormals : register(u2);
RWTexture2D<float4> AOVWorldPosition : register(u3);
RWTexture2D<float4> AOVSummedLumaSquared : register(u4);
RWTexture2D<float4> AOVCustomOutput : register(u5);

[numthreads(1, 1, 1)]
void main( uint2 DTid : SV_DispatchThreadID )
{
	AOVNormals[DTid] = float4(0.0f, 0.0f, 0.0f, 0.0f);
	AOVWorldPosition[DTid] = float4(0.0f, 0.0f, 0.0f, 0.0f);
	AOVSummedLumaSquared[DTid] = float4(0, 0, 0, 0);
	AOVCustomOutput[DTid] = float4(0.0f, 0.0f, 0.0f, 0.0f);
}