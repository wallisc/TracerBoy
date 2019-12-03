#define HLSL
#include "SharedShaderStructs.h"

cbuffer LocalConstants : register(b2)
{
	uint GeometryIndex;
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

	float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
	payload.barycentrics = attr.barycentrics;
	payload.objectIndex = GeometryIndex;
	payload.hitT = RayTCurrent();
	payload.normal =
		payload.barycentrics.x * n0 +
		payload.barycentrics.y * n1 +
		(1.0 - payload.barycentrics.x - payload.barycentrics.y) * n2;
}



