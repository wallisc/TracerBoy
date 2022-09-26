struct VertexOutput
{
	float4 pos : SV_POSITION;
	float2 tex : TEXCOORD0;
};

VertexOutput main(uint id : SV_VertexID)
{
	VertexOutput output;
	output.tex = float2((id << 1) & 2, id & 2);
	output.pos = float4(output.tex * float2(2, -2) + float2(-1, 1), 0, 1);
	return output;
}