#define HLSL
#include "SharedShaderStructs.h"

cbuffer LocalConstants : register(b2)
{
	uint GeometryIndex;
	uint MaterialIndex;
}

StructuredBuffer<uint> IndexBuffer : register(t2);
StructuredBuffer<float> VertexBuffer : register(t3);

#define VertexStride 8

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

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
	uint primitiveIndex = PrimitiveIndex();
	uint i0 = IndexBuffer[primitiveIndex * 3];
	uint i1 = IndexBuffer[primitiveIndex * 3 + 1];
	uint i2 = IndexBuffer[primitiveIndex * 3 + 2];

	const uint normalOffset = 3;
	float3 n0 = GetFloat3FromVertexBuffer(i0, normalOffset);
	float3 n1 = GetFloat3FromVertexBuffer(i1, normalOffset);
	float3 n2 = GetFloat3FromVertexBuffer(i2, normalOffset);

	const uint uvOffset = 6;
	float2 uv0 = GetFloat2FromVertexBuffer(i0, uvOffset);
	float2 uv1 = GetFloat2FromVertexBuffer(i1, uvOffset);
	float2 uv2 = GetFloat2FromVertexBuffer(i2, uvOffset);

	float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
	payload.uv = 
			barycentrics.x * uv0 +
			barycentrics.y * uv1 +
			barycentrics.z * uv2;
	payload.materialIndex = MaterialIndex;
	payload.hitT = RayTCurrent();
	payload.normal = normalize(
		barycentrics.x * n0 +
		barycentrics.y * n1 +
		barycentrics.z * n2);
}



