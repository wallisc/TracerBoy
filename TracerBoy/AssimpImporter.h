#pragma once

namespace AssimpImporter
{
	struct ScratchData
	{

	};

	// Load a scene using assimp and then convert it to a PBRT scene
	std::shared_ptr<pbrt::Scene> LoadScene(const std::string &filename, ScratchData &scratchData);
}
