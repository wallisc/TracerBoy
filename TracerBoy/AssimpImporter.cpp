#include "pch.h"

void ConvertToPBRTTexture(aiMaterial &AiMaterial, const char *key, UINT type, UINT idx, pbrt::Texture::SP &pTexture)
{
	aiString textureName;
	auto result = AiMaterial.Get(key, type, idx, textureName);
	if (textureName.length)
	{
		pTexture = std::make_shared<pbrt::ImageTexture>(textureName.C_Str());
	}
}


void ConvertToFloat(aiMaterial& AiMaterial, const char* key, UINT type, UINT idx, float &f)
{
	auto result = AiMaterial.Get(key, type, idx, f);
}

void ConvertToPBRTFloat3(aiMaterial& AiMaterial, const char* key, UINT type, UINT idx, pbrt::vec3f& vec)
{
	aiColor3D color;
	auto result = AiMaterial.Get(key, type, idx, color);
	if (result == AI_SUCCESS)
	{
		vec.x = color.r;
		vec.y = color.g;
		vec.z = color.b;
	}
}

std::shared_ptr<pbrt::Scene> AssimpImporter::LoadScene(
	const std::string& filename, 
	ScratchData& scratchData)
{
	// Create an instance of the Importer class
	Assimp::Importer importer;

	// And have it read the given file with some example postprocessing
	// Usually - if speed is not the most important aspect for you - you'll 
	// propably to request more postprocessing than we do in this example.
	const aiScene* pAiScene = importer.ReadFile(filename,
		aiProcess_CalcTangentSpace |
		aiProcess_Triangulate |
		aiProcess_JoinIdenticalVertices |
		aiProcess_MakeLeftHanded |
		aiProcess_FlipUVs |
		aiProcess_FlipWindingOrder |
		aiProcess_GenSmoothNormals |
		aiProcess_PreTransformVertices |
		aiProcess_SortByPType);

	std::shared_ptr<pbrt::Scene> pScene = std::make_shared<pbrt::Scene>();
	pScene->world = std::make_shared<pbrt::Object>();

	std::shared_ptr<pbrt::Camera> pCamera = std::make_shared<pbrt::Camera>();
	pCamera->frame = pbrt::affine3f::identity();
	pScene->cameras.push_back(pCamera);

	std::shared_ptr<pbrt::InfiniteLightSource> pLight = std::make_shared<pbrt::InfiniteLightSource>();
	pLight->mapName = "../bistro/san_giuseppe_bridge_4k.hdr";
	pScene->world->lightSources.push_back(pLight);
	UINT numMaterials = pAiScene->mNumMaterials;

	std::vector<std::shared_ptr<pbrt::Material>> materialList;
	for (UINT materialIndex = 0; materialIndex < numMaterials; materialIndex++)
	{
		aiMaterial *pAiMaterial = pAiScene->mMaterials[materialIndex];
		UINT numProperties = pAiMaterial->mNumProperties;
		
		std::shared_ptr<pbrt::UberMaterial> pMaterial = std::make_shared<pbrt::UberMaterial>();
		materialList.push_back(pMaterial);

		UINT count = pAiMaterial->GetTextureCount(aiTextureType_DIFFUSE);

		ConvertToPBRTTexture(*pAiMaterial, AI_MATKEY_TEXTURE_DIFFUSE(0), pMaterial->map_kd);
		ConvertToPBRTTexture(*pAiMaterial, AI_MATKEY_TEXTURE_NORMALS(0), pMaterial->map_normal);
		ConvertToPBRTTexture(*pAiMaterial, AI_MATKEY_TEXTURE_EMISSIVE(0), pMaterial->map_emissive);
		ConvertToPBRTTexture(*pAiMaterial, AI_MATKEY_TEXTURE_SPECULAR(0), pMaterial->map_specular);
		
#if 0
			if (materialIndex == 0)
			{
				pMaterial->map_kd = std::make_shared<pbrt::ImageTexture>("Aset_rock_volcanic_S_qcnbS_8K_Albedo.tga");
				pMaterial->map_specular = std::make_shared<pbrt::ImageTexture>("Aset_rock_volcanic_S_qcnbS_8K_Roughness.bmp");
				pMaterial->map_normal = std::make_shared<pbrt::ImageTexture>("Aset_rock_volcanic_S_qcnbS_8K_Normal_LOD0.tga");
			}
			else
			{
				pMaterial->map_kd = std::make_shared<pbrt::ImageTexture>("Aset_rock_volcanic_X_qkofQ_8K_Albedo.tga");
				pMaterial->map_specular = std::make_shared<pbrt::ImageTexture>("Aset_rock_volcanic_X_qkofQ_8K_Roughness.bmp");
				pMaterial->map_normal = std::make_shared<pbrt::ImageTexture>("Aset_rock_volcanic_X_qkofQ_8K_Normal_LOD0.tga");
			}
#endif

		ConvertToPBRTFloat3(*pAiMaterial, AI_MATKEY_COLOR_DIFFUSE, pMaterial->kd);
		
		for (UINT propertyIndex = 0; propertyIndex < numProperties; propertyIndex++)
		{
			aiMaterialProperty* pProperty = pAiMaterial->mProperties[propertyIndex];
			std::string keyName = pProperty->mKey.C_Str();

			if (keyName.compare("?mat.name") == 0)
			{
				VERIFY(pProperty->mType == aiPTI_String);
				pMaterial->name = pProperty->mData;
			}
		}
	}

	UINT numMeshes = pAiScene->mNumMeshes;
	for (UINT meshIndex = 0; meshIndex < numMeshes; meshIndex++)
	{
		aiMesh* pAiMesh = pAiScene->mMeshes[meshIndex];
		std::shared_ptr<pbrt::TriangleMesh> pMesh = std::make_shared<pbrt::TriangleMesh>();
		pMesh->material = materialList[pAiMesh->mMaterialIndex];
		pScene->world->shapes.push_back(pMesh);

		for (UINT faceIndex = 0; faceIndex < pAiMesh->mNumFaces; faceIndex++)
		{
			aiFace &face = pAiMesh->mFaces[faceIndex];
			VERIFY(face.mNumIndices == 3);

			pMesh->index.push_back({ (int)face.mIndices[0], (int)face.mIndices[1], (int)face.mIndices[2] });
		}

		for (UINT vertexIndex = 0; vertexIndex < pAiMesh->mNumVertices; vertexIndex++)
		{
			VERIFY(pAiMesh->HasPositions());
			aiVector3D& vertex = pAiMesh->mVertices[vertexIndex];
			pMesh->vertex.push_back({ vertex.x, vertex.y, vertex.z });

			if (pAiMesh->HasNormals())
			{
				aiVector3D& normal = pAiMesh->mNormals[vertexIndex];
				pMesh->normal.push_back({ normal.x, normal.y, normal.z });
			}
			
			if (pAiMesh->HasTextureCoords(0))
			{
				aiVector3D uv = pAiMesh->mTextureCoords[0][vertexIndex];
				pMesh->texcoord.push_back({ uv.x, uv.y });
			}

			if (pAiMesh->HasTangentsAndBitangents())
			{
				aiVector3D tangent = pAiMesh->mTangents[vertexIndex];
				pMesh->tangents.push_back({ tangent.x, tangent.y, tangent.z });
			}
		}
	}

	return pScene;
}