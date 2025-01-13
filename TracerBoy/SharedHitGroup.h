cbuffer LocalConstants : register(b2)
{
	uint MaterialIndex;
	uint VertexBufferIndex;
	uint VertexBufferOffset;
	uint IndexBufferIndex;
	uint IndexBufferOffset;
	uint GeometryIndex;
}

#define VertexStride 8

struct HitGroupShaderRecord
{
	uint ShaderIdentifier[8]; // 32
	uint MaterialIndex; // 4
	uint VertexBufferIndex; // 4
	uint VertexBufferOffsetInVertices; // 4
	uint IndexBufferIndex; // 4
	uint IndexBufferOffset; // 4
	uint GeometryIndex; // 4
	uint Padding[4]; // 16
};

StructuredBuffer<HitGroupShaderRecord> ShaderTable: register(t11);
Buffer<uint> IndexBuffers[] : register(t0, space2);
StructuredBuffer<Vertex> VertexBuffers[] : register(t0, space3);

struct GeometryInfo
{
	uint MaterialIndex;
	uint IndexBufferIndex;
	uint IndexBufferFirstElement;
	uint VertexBufferIndex;
	uint VertexBufferFirstElement;
};

StructuredBuffer<Vertex> GetVertexBuffer(uint Index)
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

	const uint VertexBufferElementSize = 4;
	const uint IndexBufferElementSize = 4;

	GeometryInfo info;
	info.MaterialIndex = ShaderRecord.MaterialIndex;
	info.VertexBufferIndex = ShaderRecord.VertexBufferIndex;
	info.VertexBufferFirstElement = ShaderRecord.VertexBufferOffsetInVertices;
	info.IndexBufferIndex = ShaderRecord.IndexBufferIndex;
	info.IndexBufferFirstElement = ShaderRecord.IndexBufferOffset / IndexBufferElementSize;
	return info;
}

struct HitInfo
{
	float2 uv;
	float3 normal;
	float3 tangent;
};

Vertex GetVertex(GeometryInfo Geometry, uint vertexIndex)
{
	StructuredBuffer<Vertex> VertexBuffer = GetVertexBuffer(Geometry.VertexBufferIndex);
	return VertexBuffer[vertexIndex + Geometry.VertexBufferFirstElement];
}

uint3 GetIndices(GeometryInfo Geometry, uint PrimitiveIndex)
{
	Buffer<uint> IndexBuffer = GetIndexBuffer(Geometry.IndexBufferIndex);
	return uint3(
		IndexBuffer[Geometry.IndexBufferFirstElement + PrimitiveIndex * 3],
		IndexBuffer[Geometry.IndexBufferFirstElement + PrimitiveIndex * 3 + 1], 
		IndexBuffer[Geometry.IndexBufferFirstElement + PrimitiveIndex * 3 + 2]);
}

float2 GetVertexUV(GeometryInfo Geometry, uint vertexIndex)
{
	Vertex vertex = GetVertex(Geometry, vertexIndex);
	return float2(vertex.UV0, vertex.UV1);
}


float2 GetUV(GeometryInfo Geometry, uint3 indices, float3 barycentrics)
{
	float2 uv0 = GetVertexUV(Geometry, indices.x);
	float2 uv1 = GetVertexUV(Geometry, indices.y);
	float2 uv2 = GetVertexUV(Geometry, indices.z);

	return
		barycentrics.x * uv0 +
		barycentrics.y * uv1 +
		barycentrics.z * uv2;
}

float3 GetNormal(GeometryInfo Geometry, uint3 indices, float3 barycentrics)
{
	float3 n0 = GetVertex(Geometry, indices.x).Normal;
	float3 n1 = GetVertex(Geometry, indices.y).Normal;
	float3 n2 = GetVertex(Geometry, indices.z).Normal;

	return normalize(
		barycentrics.x * n0 +
		barycentrics.y * n1 +
		barycentrics.z * n2);
}

float3 GetTangent(GeometryInfo Geometry, uint3 indices, float3 barycentrics)
{
	float3 t0 = GetVertex(Geometry, indices.x).Tangent;
	float3 t1 = GetVertex(Geometry, indices.y).Tangent;
	float3 t2 = GetVertex(Geometry, indices.z).Tangent;

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
		