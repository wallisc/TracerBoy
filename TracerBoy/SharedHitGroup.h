cbuffer LocalConstants : register(b2)
{
	uint GeometryIndex;
	uint MaterialIndex;
}

StructuredBuffer<uint> IndexBuffer : register(t2);
StructuredBuffer<float> VertexBuffer : register(t3);

#define VertexStride 11

float3 GetFloat3FromVertexBuffer(uint vertexIndex, uint dataOffset)
{
	return float3(
		VertexBuffer[VertexStride * vertexIndex + dataOffset],
		VertexBuffer[VertexStride * vertexIndex + dataOffset + 1],
		VertexBuffer[VertexStride * vertexIndex + dataOffset + 2]);
}

float2 GetFloat2FromVertexBuffer(uint vertexIndex, uint dataOffset)
{
	return float2(
		VertexBuffer[VertexStride * vertexIndex + dataOffset],
		VertexBuffer[VertexStride * vertexIndex + dataOffset + 1]);
}

uint3 GetIndices(uint PrimitiveIndex)
{
	return uint3(IndexBuffer[PrimitiveIndex * 3], IndexBuffer[PrimitiveIndex * 3 + 1], IndexBuffer[PrimitiveIndex * 3 + 2]);
}

float2 GetUV(uint3 indices, float3 barycentrics)
{
	const uint uvOffset = 6;
	float2 uv0 = GetFloat2FromVertexBuffer(indices.x, uvOffset);
	float2 uv1 = GetFloat2FromVertexBuffer(indices.y, uvOffset);
	float2 uv2 = GetFloat2FromVertexBuffer(indices.z, uvOffset);

	return
		barycentrics.x * uv0 +
		barycentrics.y * uv1 +
		barycentrics.z * uv2;
}

float3 GetNormal(uint3 indices, float3 barycentrics)
{
	const uint normalOffset = 3;
	float3 n0 = GetFloat3FromVertexBuffer(indices.x, normalOffset);
	float3 n1 = GetFloat3FromVertexBuffer(indices.y, normalOffset);
	float3 n2 = GetFloat3FromVertexBuffer(indices.z, normalOffset);

	return normalize(
		barycentrics.x * n0 +
		barycentrics.y * n1 +
		barycentrics.z * n2);
}

float3 GetTangent(uint3 indices, float3 barycentrics)
{
	const uint tangentOffset = 8;
	float3 t0 = GetFloat3FromVertexBuffer(indices.x, tangentOffset);
	float3 t1 = GetFloat3FromVertexBuffer(indices.y, tangentOffset);
	float3 t2 = GetFloat3FromVertexBuffer(indices.z, tangentOffset);
	return normalize(
		barycentrics.x * t0 +
		barycentrics.y * t1 +
		barycentrics.z * t2);
}

float3 GetBarycentrics(BuiltInTriangleIntersectionAttributes attr)
{
	return float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
}