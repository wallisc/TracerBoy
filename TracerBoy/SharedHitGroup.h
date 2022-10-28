cbuffer LocalConstants : register(b2)
{
	uint MaterialIndex;
	uint VertexBufferIndex;
	uint IndexBufferIndex;
	uint GeometryIndex;
}

#define VertexStride 12

struct HitGroupShaderRecord
{
	uint ShaderIdentifier[8]; // 32
	uint MaterialIndex; // 4
	uint VertexBufferIndex; // 4
	uint IndexBufferIndex; // 4
	uint GeometryIndex; // 4
	uint Padding[4]; // 16
};

StructuredBuffer<HitGroupShaderRecord> ShaderTable: register(t11);
Buffer<uint> IndexBuffers[] : register(t0, space2);
Buffer<float> VertexBuffers[] : register(t0, space3);

struct GeometryInfo
{
	uint MaterialIndex;
	uint IndexBufferIndex;
	uint VertexBufferIndex;
};

Buffer<float> GetVertexBuffer(uint Index)
{
	return VertexBuffers[NonUniformResourceIndex(Index)];
}

Buffer<uint> GetIndexBuffer(uint Index)
{
	return IndexBuffers[NonUniformResourceIndex(Index)];
}

GeometryInfo GetGeometryInfo(uint GeometryIndex)
{
	HitGroupShaderRecord ShaderRecord = ShaderTable[GeometryIndex];
	GeometryInfo info;
	info.MaterialIndex = ShaderRecord.MaterialIndex;
	info.VertexBufferIndex = ShaderRecord.VertexBufferIndex;
	info.IndexBufferIndex = ShaderRecord.IndexBufferIndex;
	return info;
}

struct HitInfo
{
	float2 uv;
	float3 normal;
	float3 tangent;
};

float3 GetFloat3FromVertexBuffer(GeometryInfo Geometry, uint vertexIndex, uint dataOffset)
{
	Buffer<float> VertexBuffer = GetVertexBuffer(Geometry.VertexBufferIndex);
	return float3(
		VertexBuffer[VertexStride * vertexIndex + dataOffset],
		VertexBuffer[VertexStride * vertexIndex + dataOffset + 1],
		VertexBuffer[VertexStride * vertexIndex + dataOffset + 2]);
}

float2 GetFloat2FromVertexBuffer(GeometryInfo Geometry, uint vertexIndex, uint dataOffset)
{
	Buffer<float> VertexBuffer = GetVertexBuffer(Geometry.VertexBufferIndex);
	return float2(
		VertexBuffer[VertexStride * vertexIndex + dataOffset],
		VertexBuffer[VertexStride * vertexIndex + dataOffset + 1]);
}

uint3 GetIndices(GeometryInfo Geometry, uint PrimitiveIndex)
{
	Buffer<uint> IndexBuffer = GetIndexBuffer(Geometry.IndexBufferIndex);
	return uint3(IndexBuffer[PrimitiveIndex * 3], IndexBuffer[PrimitiveIndex * 3 + 1], IndexBuffer[PrimitiveIndex * 3 + 2]);
}

float2 GetUV(GeometryInfo Geometry, uint3 indices, float3 barycentrics)
{
	const uint uvOffset = 6;
	float2 uv0 = GetFloat2FromVertexBuffer(Geometry, indices.x, uvOffset);
	float2 uv1 = GetFloat2FromVertexBuffer(Geometry, indices.y, uvOffset);
	float2 uv2 = GetFloat2FromVertexBuffer(Geometry, indices.z, uvOffset);

	return
		barycentrics.x * uv0 +
		barycentrics.y * uv1 +
		barycentrics.z * uv2;
}

float3 GetNormal(GeometryInfo Geometry, uint3 indices, float3 barycentrics)
{
	const uint normalOffset = 3;
	float3 n0 = GetFloat3FromVertexBuffer(Geometry, indices.x, normalOffset);
	float3 n1 = GetFloat3FromVertexBuffer(Geometry, indices.y, normalOffset);
	float3 n2 = GetFloat3FromVertexBuffer(Geometry, indices.z, normalOffset);

	return normalize(
		barycentrics.x * n0 +
		barycentrics.y * n1 +
		barycentrics.z * n2);
}

float3 GetTangent(GeometryInfo Geometry, uint3 indices, float3 barycentrics)
{
	const uint tangentOffset = 8;
	float3 t0 = GetFloat3FromVertexBuffer(Geometry, indices.x, tangentOffset);
	float3 t1 = GetFloat3FromVertexBuffer(Geometry, indices.y, tangentOffset);
	float3 t2 = GetFloat3FromVertexBuffer(Geometry, indices.z, tangentOffset);
	return normalize(
		barycentrics.x * t0 +
		barycentrics.y * t1 +
		barycentrics.z * t2);
}

float3 GetBarycentrics3(float2 barycentrics)
{
	return float3(1 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
}

HitInfo GetHitInfo(GeometryInfo Geometry, uint PrimitiveIndex, float3 barycentrics)
{
	HitInfo info;
	uint3 indices = GetIndices(Geometry, PrimitiveIndex);

	info.uv = GetUV(Geometry, indices, barycentrics);
	info.normal = GetNormal(Geometry, indices, barycentrics);

	// TODO: Only do this if there is a normal map. Should have this specified in some geometry flag
	info.tangent = GetTangent(Geometry, indices, barycentrics);
	return info;
}

Material GetMaterial_NonRecursive(int MaterialID);
bool IsValidTexture(uint textureIndex);
float4 GetTextureData(uint textureIndex, float2 uv);

bool IsValidHit(GeometryInfo Geometry, HitInfo Hit)
{
	bool bIsValidHit = true;
	Material mat = GetMaterial_NonRecursive(Geometry.MaterialIndex);
	float2 uv = Hit.uv;
	if (IsValidTexture(mat.alphaIndex))
	{
		float alpha = GetTextureData(mat.alphaIndex, uv).r;
		if (alpha < 0.9f)
		{
			bIsValidHit = false;
		}
	}
	else if (IsValidTexture(mat.albedoIndex))
	{
		float alpha = GetTextureData(mat.albedoIndex, uv).a;
		if (alpha < 0.9f)
		{
			bIsValidHit = false;
		}
	}
	return bIsValidHit;
}
		