#include "pch.h"

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
	pLight->mapName = "../dragon/textures/envmap.hdr";
	pScene->world->lightSources.push_back(pLight);
#if 0
	UINT numMaterials = pAiScene->mNumMaterials;
	for (UINT materialIndex = 0; materialIndex < numMaterials; materialIndex++)
	{
		aiMaterial *pAiMaterial = pAiScene->mMaterials[materialIndex];
		UINT numProperties = pAiMaterial->mNumProperties;
	}
#endif

#if 0
	std::shared_ptr<pbrt::DisneyMaterial> pDefaultMaterial = std::make_shared<pbrt::DisneyMaterial>();
	pDefaultMaterial->metallic = 1.0f;
	pDefaultMaterial->color.x = 0.55f;
	pDefaultMaterial->color.y = 0.2f;
	pDefaultMaterial->color.z = 0.075f;
	pDefaultMaterial->roughness = 0.1f;
#endif

	//MakeNamedMaterial "Rough Ice" "string type" "uber" "rgb Kd"[0.65 0.65 0.8] "float index" 1.1 "rgb opacity"[0 0 0] "float roughness" 0.3 "rgb Kt"[0 0 0]
	std::shared_ptr<pbrt::UberMaterial> pDefaultMaterial = std::make_shared<pbrt::UberMaterial>();
	pDefaultMaterial->kd.x = 1.0f;
	pDefaultMaterial->kd.y = 0.6f;
	pDefaultMaterial->kd.z = 0.6f;
	pDefaultMaterial->index = 1.05f;
	pDefaultMaterial->opacity.x = 0.0f;
	pDefaultMaterial->opacity.y = 0.0f;
	pDefaultMaterial->opacity.z = 0.0f;
	pDefaultMaterial->kt.x = 0.5f;
	pDefaultMaterial->kt.y = 0.5f;
	pDefaultMaterial->kt.z = 0.5f;
	pDefaultMaterial->roughness = 0.5f;


	UINT numMeshes = pAiScene->mNumMeshes;
	for (UINT meshIndex = 0; meshIndex < numMeshes; meshIndex++)
	{
		aiMesh* pAiMesh = pAiScene->mMeshes[meshIndex];
		std::shared_ptr<pbrt::TriangleMesh> pMesh = std::make_shared<pbrt::TriangleMesh>();
		pMesh->material = pDefaultMaterial;
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
			
#if 0
			if (pAiMesh->HasTextureCoords(0))
			{
				aiVector3D* uv = pAiMesh->mTextureCoords[vertexIndex];
				pMesh->texcoord.push_back({ uv[0].x, uv[0].y });
			}
#endif
		}
	}

	return pScene;
}