#pragma once
struct Vector3
{
	Vector3(float nX = 0.0f, float nY = 0.0f, float nZ = 0.0f) : x(nX), y(nY), z(nZ) {}
	float x, y, z;

	Vector3 operator*(float scale)
	{
		return { x * scale, y * scale, z * scale };
	}

	Vector3 operator+=(const Vector3& v)
	{
		*this = v + *this;
		return *this;
	}

	Vector3 operator-=(const Vector3& v)
	{
		*this = *this - v;
		return *this;
	}

	Vector3 operator+(const Vector3 &v) const
	{
		return { x + v.x, y + v.y, z + v.z };
	}

	Vector3 operator-(const Vector3& v) const
	{
		return { x - v.x, y - v.y, z - v.z };
	}

	Vector3 Normalize() const
	{
		float length = sqrtf(x * x + y * y + z * z);
		return { x / length, y / length, z / length };
	}
};

struct PassResource
{
	PassResource() {}

	ComPtr<ID3D12Resource> m_pResource;
	D3D12_GPU_DESCRIPTOR_HANDLE m_srvHandle;
	D3D12_GPU_DESCRIPTOR_HANDLE m_uavHandle;
};

inline Vector3 Cross(const Vector3& a, const Vector3& b)
{
	return Vector3(
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x);
}

struct Camera
{
	Vector3 Position;
	Vector3 LookAt;
	Vector3 Right;
	Vector3 Up;
	float LensHeight;
	float FocalDistance;
};

struct ScopedResourceBarrier
{
	ScopedResourceBarrier(ID3D12GraphicsCommandList& commandList, ID3D12Resource *pResource, D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState) :
		m_commandList(commandList),
		m_pResource(pResource),
		m_beforeState(beforeState),
		m_afterState(afterState)
	{
		if (m_pResource)
		{
			auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_pResource, m_beforeState, m_afterState);
			m_commandList.ResourceBarrier(1, &barrier);
		}
	}

	~ScopedResourceBarrier()
	{
		if (m_pResource)
		{
			auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_pResource, m_afterState, m_beforeState);
			m_commandList.ResourceBarrier(1, &barrier);
		}
	}

private:
	ID3D12GraphicsCommandList& m_commandList;
	ID3D12Resource* m_pResource;
	D3D12_RESOURCE_STATES m_beforeState;
	D3D12_RESOURCE_STATES m_afterState;
};

class TextureAllocator;
class DenoiserPass;

class TracerBoy
{
public:
		
	TracerBoy(ID3D12CommandQueue *pQueue);

	void LoadScene(
		ID3D12GraphicsCommandList& commandList, 
		const std::string& sceneFileName, 
		std::vector<ComPtr<ID3D12Resource>> &resourcesToDelete);

	enum class OutputType
	{
		Lit,
		Albedo,
		Normals
	};

	struct CameraOutputSettings
	{
		float m_FocalDistance;
		float m_DOFFocalDistance;
		float m_ApertureWidth;
	};

	struct FogSettings
	{
		float ScatterDistance;
		float ScatterDirection;
	};

	struct PostProcessSettings
	{
		float m_ExposureMultiplier;
		bool m_bEnableToneMapping;
		bool m_bEnableGammaCorrection;
	};


	struct OutputSettings
	{
		OutputType m_OutputType;
		bool m_EnableNormalMaps;

		PostProcessSettings m_postProcessSettings;
		CameraOutputSettings m_cameraSettings;
		DenoiserSettings m_denoiserSettings;
		FogSettings m_fogSettings;
	};

	static OutputSettings GetDefaultOutputSettings()
	{
		OutputSettings outputSettings;
		outputSettings.m_OutputType = OutputType::Lit;
		outputSettings.m_EnableNormalMaps = false;
		
		PostProcessSettings& postProcessSettings = outputSettings.m_postProcessSettings;
		postProcessSettings.m_ExposureMultiplier = 1.0f;
		postProcessSettings.m_bEnableToneMapping = true;
		postProcessSettings.m_bEnableGammaCorrection = true;

		CameraOutputSettings& cameraSettings = outputSettings.m_cameraSettings;
		cameraSettings.m_FocalDistance = 3.0f;
		cameraSettings.m_DOFFocalDistance = 0.0f;
		cameraSettings.m_ApertureWidth = 0.075f;

		DenoiserSettings &denoiserSettings = outputSettings.m_denoiserSettings;
		denoiserSettings.m_bEnabled = false;
		denoiserSettings.m_intersectPositionWeightingMultiplier = 1.0f;
		denoiserSettings.m_normalWeightingExponential = 128.0f;
		denoiserSettings.m_luminanceWeightingMultiplier = 4.0f;
		denoiserSettings.m_waveletIterations = 5;
		denoiserSettings.m_fireflyClampValue = 3.0f;

		FogSettings& fogSettings = outputSettings.m_fogSettings;
		fogSettings.ScatterDistance = 0.0f;
		fogSettings.ScatterDirection = 0.0f;

		return outputSettings;
	}

	void Render(ID3D12GraphicsCommandList &commandList, ID3D12Resource *pBackBuffer, const OutputSettings &outputSettings);

	struct CameraSettings
	{
		float m_movementSpeed;
		bool m_ignoreMouse;
	};
	void Update(int mouseX, int mouseY, bool keyboardInput[CHAR_MAX], float dt, const CameraSettings &cameraSettings);

	friend class TextureAllocator;
private:
	void UpdateOutputSettings(const OutputSettings& outputSettings);
	D3D12_GPU_DESCRIPTOR_HANDLE GetOutputSRV(OutputType outputType);

	void ResizeBuffersIfNeeded(ID3D12Resource *pBackBuffer);
	void UpdateConfigConstants(UINT backBufferWidth, UINT backBufferHeight);

	void InitializeLocalRootSignature();
	void InitializeTexture(
		const std::wstring& textureName,
		ID3D12GraphicsCommandList& commandList,
		ComPtr<ID3D12Resource>& pResource,
		UINT SRVSlot,
		ComPtr<ID3D12Resource>& pUploadResource);

	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(UINT slot);
	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(UINT slot);

	void AllocateUploadBuffer(UINT bufferSize, ComPtr<ID3D12Resource>& pBuffer);
	void AllocateBufferWithData(const void *pData, UINT dataSize, ComPtr<ID3D12Resource> &pBuffer);

	UINT CurrentDescriptorSlot;
	UINT AllocateDescriptorHeapSlot()
	{
		VERIFY(CurrentDescriptorSlot < ViewDescriptorHeapSlots::NumTotalViews);
		return CurrentDescriptorSlot++;
	}

	ComPtr<ID3D12Device5> m_pDevice;
	ComPtr<ID3D12CommandQueue> m_pCommandQueue;
	ComPtr<ID3D12DescriptorHeap> m_pViewDescriptorHeap;

	ComPtr<ID3D12Resource> m_pBottomLevelAS;
	ComPtr<ID3D12Resource> m_pTopLevelAS;
	ComPtr<ID3D12Resource> m_pConfigConstants;

	ComPtr<ID3D12Resource> m_pEnvironmentMap;
	ComPtr<ID3D12Resource> m_pMaterialList;
	ComPtr<ID3D12Resource> m_pTextureDataList;

	std::vector<ComPtr<ID3D12Resource>> m_pBuffers;
	std::vector<ComPtr<ID3D12Resource>> m_pTextures;

	const DXGI_FORMAT RayTracingOutputFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
	enum OutputUAVs
	{
		PathTracerOutputUAV0 = 0,
		PathTracerOutputUAV1,
		NumPathTracerOutputUAVs
	};
	static const UINT MaxActiveFrames = NumPathTracerOutputUAVs;

	ComPtr<ID3D12Resource> m_pRandSeedBuffer[MaxActiveFrames];
	void UpdateRandSeedBuffer(UINT bufferIndex);

	ComPtr<ID3D12Resource> m_pAccumulatedPathTracerOutput[OutputUAVs::NumPathTracerOutputUAVs];
	UINT8 m_ActiveFrameIndex;

	enum LocalRayTracingRootSignatureParameters
	{
		GeometryIndexRootConstant = 0,
		IndexBufferSRV,
		VertexBufferSRV,
		NumLocalRayTracingParameters
	};

	enum RayTracingRootSignatureParameters
	{
		PerFrameConstantsParam = 0,
		ConfigConstantsParam,
		EnvironmentMapSRV,
		LastFrameSRV,
		AOVDescriptorTable,
		OutputUAV,
		AccelerationStructureRootSRV,
		RandSeedRootSRV,
		MaterialBufferSRV,
		TextureDataSRV,
		ImageTextureTable,
		NumRayTracingParameters
	};
	
	ComPtr<ID3D12RootSignature> m_pLocalRootSignature;

	ComPtr<ID3D12RootSignature> m_pRayTracingRootSignature;
	ComPtr<ID3D12StateObject> m_pRayTracingStateObject;

	ComPtr<ID3D12Resource> m_pRayGenShaderTable;
	ComPtr<ID3D12Resource> m_pHitGroupShaderTable;
	ComPtr<ID3D12Resource> m_pMissShaderTable;


	enum PostProcessRootSignatureParameters
	{
		InputTexture = 0,
		OutputTexture,
		Constants,
		NumParameters
	};
	ComPtr<ID3D12RootSignature> m_pPostProcessRootSignature;
	ComPtr<ID3D12PipelineState> m_pPostProcessPSO;

	ComPtr<ID3D12PipelineState> m_pClearAOVs;

	UINT32 m_mouseX, m_mouseY;
	UINT m_FramesRendered;
	bool m_bInvalidateHistory;

	ComPtr<ID3D12Resource> m_pPostProcessOutput;
	PassResource m_pDenoiserBuffers[2];

	ComPtr<ID3D12Resource> m_pAOVNormals;
	ComPtr<ID3D12Resource> m_pAOVAlbedo;
	ComPtr<ID3D12Resource> m_pAOVWorldPosition;
	ComPtr<ID3D12Resource> m_pAOVSDRHistogram;

	ComPtr<ID3D12Resource> CreateUAV(const D3D12_RESOURCE_DESC& uavDesc, D3D12_CPU_DESCRIPTOR_HANDLE);
	ComPtr<ID3D12Resource> CreateUAVandSRV(const D3D12_RESOURCE_DESC& uavDesc, D3D12_CPU_DESCRIPTOR_HANDLE uavHandle, D3D12_CPU_DESCRIPTOR_HANDLE srvHandle);

	enum ViewDescriptorHeapSlots
	{
		PostProcessOutputUAV = 0,
		DenoiserOuputBaseSRV,
		DenoiserOuputLastSRV,
		DenoiserOutputBaseUAV,
		DenoiserOutputLastUAV,
		EnvironmentMapSRVSlot,
		AOVBaseUAVSlot,
		AOVNormalsUAV = AOVBaseUAVSlot,
		AOVWorldPositionUAV,
		AOVSDRHistogramUAV,
		AOVAlbedoUAV,
		AOVLastUAVSlot = AOVAlbedoUAV,
		AOVBaseSRVSlot,
		AOVNormalsSRV = AOVBaseSRVSlot,
		AOVWorldPositionSRV,
		AOVSDRHistogramSRV,
		AOVAlbedoSRV,
		AOVLastSRVSlot = AOVAlbedoSRV,
		PathTracerOutputSRVBaseSlot,
		PathTracerOutputSRVLastSlot = PathTracerOutputSRVBaseSlot + OutputUAVs::NumPathTracerOutputUAVs - 1,
		PathTracerOutputUAVBaseSlot,
		PathTracerOutputUAVLastSlot = PathTracerOutputUAVBaseSlot + OutputUAVs::NumPathTracerOutputUAVs - 1,
		NumReservedViewSlots,
		NumTotalViews = 4096
	};

	OutputSettings m_CachedOutputSettings;
	Camera m_camera;
	bool m_flipTextureUVs;

	std::string m_sceneFileDirectory;
	std::unique_ptr<DenoiserPass> m_pDenoiserPass;
};

class TextureAllocator
{
public:
	TextureAllocator(TracerBoy& tracerBoy, ID3D12GraphicsCommandList& CommandList) :
		m_tracerboy(tracerBoy),
		m_pCommandList(&CommandList)
	{}

	UINT CreateTexture(pbrt::Texture::SP& pPbrtTexture, bool bGammaCorrect = false);
	const std::vector<TextureData>& GetTextureData() const { return m_textureData; }

	void ExtractScratchResources(std::vector<ComPtr<ID3D12Resource>>& scratchResources)
	{
		for (auto &uploadResource : m_uploadResources)
		{
			scratchResources.push_back(uploadResource);
		}
	}
private:
	std::vector<TextureData> m_textureData;

	TracerBoy& m_tracerboy;
	std::vector<ComPtr<ID3D12Resource>> m_uploadResources;
	ComPtr<ID3D12GraphicsCommandList> m_pCommandList;
};