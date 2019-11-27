struct FULLSCREEN_PLANE_VS_OUTPUT
{
	float4 Pos : SV_POSITION;
	float2 UV : TEXCOORD0;
};

FULLSCREEN_PLANE_VS_OUTPUT VS(uint id : SV_VertexID)
{
	FULLSCREEN_PLANE_VS_OUTPUT output;
	output.UV = float2((id << 1) & 2, id & 2);
	output.Pos = float4(output.UV * float2(2, -2) + float2(-1, 1), 0, 1);
	return output;
}