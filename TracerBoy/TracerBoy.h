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
		Normals,
		Luminance,
		LuminanceVariance,
		LivePixels,
		LiveWaves,
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

	struct PerformanceSettings
	{
		int m_SampleTarget;
		float m_VarianceMultiplier;
		float m_TargetFrameRate;
		float m_ConvergencePercentage;
		bool m_bEnableBlueNoise;
		bool m_bEnableInlineRaytracing;
		bool m_bEnableExecuteIndirect;
		int m_OccupancyMultiplier;
	};

	struct DebugSettings 		
	{
		int m_SampleLimit;
		float m_TimeLimitInSeconds;
		float m_VarianceMultiplier;
	};


	struct OutputSettings
	{
		OutputType m_OutputType;

		bool m_EnableNormalMaps;

		DebugSettings m_debugSettings;
		PostProcessSettings m_postProcessSettings;
		CameraOutputSettings m_cameraSettings;
		DenoiserSettings m_denoiserSettings;
		FogSettings m_fogSettings;
		PerformanceSettings m_performanceSettings;
	};

	static OutputSettings GetDefaultOutputSettings()
	{
		OutputSettings outputSettings;
		outputSettings.m_OutputType = OutputType::Lit;
		outputSettings.m_EnableNormalMaps = false;

		DebugSettings &debugSettings = outputSettings.m_debugSettings;
		debugSettings.m_VarianceMultiplier = 1.0f;
		debugSettings.m_SampleLimit = 0;
		debugSettings.m_TimeLimitInSeconds = 0.0f;

		PostProcessSettings& postProcessSettings = outputSettings.m_postProcessSettings;
		postProcessSettings.m_ExposureMultiplier = 1.0f;
		postProcessSettings.m_bEnableToneMapping = false;
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
		denoiserSettings.m_fireflyClampValue = 0.0f;

		FogSettings& fogSettings = outputSettings.m_fogSettings;
		fogSettings.ScatterDistance = 0.0f;
		fogSettings.ScatterDirection = 0.0f;

		PerformanceSettings& performanceSettings = outputSettings.m_performanceSettings;
		performanceSettings.m_SampleTarget = 256;
		performanceSettings.m_VarianceMultiplier = 1.0f;
		performanceSettings.m_TargetFrameRate = 0.0f;
		performanceSettings.m_ConvergencePercentage = 0.001;
		performanceSettings.m_bEnableBlueNoise = false;
		performanceSettings.m_bEnableInlineRaytracing = true;
		performanceSettings.m_bEnableExecuteIndirect = true;
		performanceSettings.m_OccupancyMultiplier = 10;

		return outputSettings;
	}

	struct ReadbackStats
	{
		UINT ActiveWaves;
	};

	void Render(ID3D12GraphicsCommandList &commandList, ID3D12Resource *pBackBuffer, ID3D12Resource *pReadbackStats, const OutputSettings &outputSettings);

	UINT GetNumberOfSamplesSinceLastInvalidate() 
	{
		return m_SamplesRendered;
	}

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
		ComPtr<ID3D12Resource>& pUploadResource,
		bool bIsInternalAsset = false);

	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(UINT slot);
	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(UINT slot);

	D3D12_CPU_DESCRIPTOR_HANDLE GetNonShaderVisibleCPUDescriptorHandle(UINT slot);


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
	ComPtr<ID3D12DescriptorHeap> m_pNonShaderVisibleDescriptorHeap;

	bool m_bSupportsInlineRaytracing;

	ComPtr<ID3D12Resource> m_pBottomLevelAS;
	ComPtr<ID3D12Resource> m_pTopLevelAS;
	ComPtr<ID3D12Resource> m_pConfigConstants;

	ComPtr<ID3D12Resource> m_pEnvironmentMap;
	pbrt::math::mat3f m_EnvironmentMapTransform;

	enum WaveCompactionRootSignatureParameters
	{
		WaveCompactionConstantsParam = 0,
		WaveCompactionOutputUAV,
		WaveCompactionJitteredOutputUAV,
		WaveCompactionRayIndexBuffer,
		IndirectArgs,
		NumCompactionParameters
	};

	UINT m_MinWaveAmount;

	ComPtr<ID3D12RootSignature> m_pWaveCompactionRootSignature;
	ComPtr<ID3D12PipelineState> m_pWaveCompactionPSO;

	ComPtr<ID3D12Resource> m_pRayIndexBuffer;
	ComPtr<ID3D12Resource> m_pExecuteIndirectArgs;
	ComPtr<ID3D12CommandSignature> m_pCommandSignature;

	ComPtr<ID3D12Resource> m_pStatsBuffer;

	ComPtr<ID3D12Resource> m_pMaterialList;
	ComPtr<ID3D12Resource> m_pTextureDataList;

	std::vector<ComPtr<ID3D12Resource>> m_pBuffers;
	std::vector<ComPtr<ID3D12Resource>> m_pTextures;

	const DXGI_FORMAT RayTracingOutputFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
	static const UINT MaxActiveFrames = 2;

	ComPtr<ID3D12Resource> m_pAccumulatedPathTracerOutput;
	ComPtr<ID3D12Resource> m_pJitteredAccumulatedPathTracerOutput;
	UINT8 m_ActiveFrameIndex;

	enum LocalRayTracingRootSignatureParameters
	{
		GeometryIndexRootConstant = 0,
		NumLocalRayTracingParameters
	};

	enum RayTracingRootSignatureParameters
	{
		PerFrameConstantsParam = 0,
		ConfigConstantsParam,
		EnvironmentMapSRV,
		AOVDescriptorTable,
		OutputUAV,
		JitteredOutputUAV,
		AccelerationStructureRootSRV,
		MaterialBufferSRV,
		BlueNoise0SRV,
		BlueNoise1SRV,
		TextureDataSRV,
		ImageTextureTable,
		LuminanceVarianceParam,
		VolumeSRVParam,
		ShaderTable,
		RayIndexBuffer,
		IndirectArgsBuffer,
		StatsBuffer,
		NumRayTracingParameters
	};
	
	ComPtr<ID3D12RootSignature> m_pLocalRootSignature;

	ComPtr<ID3D12RootSignature> m_pRayTracingRootSignature;
	ComPtr<ID3D12StateObject> m_pRayTracingStateObject;
	ComPtr<ID3D12PipelineState> m_pRayTracingPSO;

	ComPtr<ID3D12Resource> m_pRayGenShaderTable;
	ComPtr<ID3D12Resource> m_pHitGroupShaderTable;
	ComPtr<ID3D12Resource> m_pMissShaderTable;


	enum PostProcessRootSignatureParameters
	{
		InputTexture = 0,
		AuxTexture,
		OutputTexture,
		Constants,
		NumParameters
	};
	ComPtr<ID3D12RootSignature> m_pPostProcessRootSignature;
	ComPtr<ID3D12PipelineState> m_pPostProcessPSO;

	ComPtr<ID3D12PipelineState> m_pClearAOVs;

	UINT32 m_mouseX, m_mouseY;
	UINT m_SamplesRendered;
	bool m_bInvalidateHistory;

	ComPtr<ID3D12Resource> m_pPostProcessOutput;
	PassResource m_pDenoiserBuffers[2];

	ComPtr<ID3D12Resource> m_pAOVNormals;
	ComPtr<ID3D12Resource> m_pAOVCustomOutput;
	ComPtr<ID3D12Resource> m_pAOVWorldPosition;
	ComPtr<ID3D12Resource> m_pAOVLumaSquared;
	ComPtr<ID3D12Resource> m_pLuminanceVariance;
	ComPtr<ID3D12Resource> m_pVolume;
	ComPtr<ID3D12Resource> m_pBlueNoise0Texture;
	ComPtr<ID3D12Resource> m_pBlueNoise1Texture;

	
	ComPtr<ID3D12Resource> CreateUAV(const std::wstring& resourceName, const D3D12_RESOURCE_DESC& uavDesc, D3D12_CPU_DESCRIPTOR_HANDLE *, D3D12_RESOURCE_STATES defaultState);
	ComPtr<ID3D12Resource> CreateSRV(const std::wstring& resourceName, const D3D12_RESOURCE_DESC& resourceDesc, const D3D12_SHADER_RESOURCE_VIEW_DESC &srvDesc, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_RESOURCE_STATES defaultState);
	ComPtr<ID3D12Resource> CreateUAVandSRV(const std::wstring& resourceName, const D3D12_RESOURCE_DESC& uavDesc, D3D12_CPU_DESCRIPTOR_HANDLE uavHandle, D3D12_CPU_DESCRIPTOR_HANDLE srvHandle, D3D12_RESOURCE_STATES defaultState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	enum ViewDescriptorHeapSlots
	{
		PostProcessOutputUAV = 0,
		DenoiserOuputBaseSRV,
		DenoiserOuputLastSRV,
		DenoiserOutputBaseUAV,
		DenoiserOutputLastUAV,
		BlueNoise0SRVSlot,
		BlueNoise1SRVSlot,
		EnvironmentMapSRVSlot,
		AOVBaseUAVSlot,
		AOVNormalsUAV = AOVBaseUAVSlot,
		AOVWorldPositionUAV,
		AOVSummedLumaSquaredUAV,
		AOVCustomOutputUAV,
		AOVLastUAVSlot = AOVCustomOutputUAV,
		AOVBaseSRVSlot,
		AOVNormalsSRV = AOVBaseSRVSlot,
		AOVWorldPositionSRV,
		AOVSummedLumaSquaredSRV,
		AOVCustomOutputSRV,
		AOVLastSRVSlot = AOVCustomOutputSRV,
		LuminanceVarianceSRV,
		LuminanceVarianceUAV,
		VolumeSRVSlot,
		PathTracerOutputSRV,
		PathTracerOutputUAV,
		JitteredPathTracerOutputSRV,
		JitteredPathTracerOutputUAV,
		RayIndexBufferUAV,
		IndirectArgsUAV,
		StatsBufferUAV,
		NumReservedViewSlots,
		NumTotalViews = 1024 * 512
	};

	Vector3 m_volumeMax;
	Vector3 m_volumeMin;

	OutputSettings m_CachedOutputSettings;
	Camera m_camera;
	bool m_flipTextureUVs;

	std::chrono::steady_clock::time_point m_RenderStartTime;

	std::chrono::steady_clock::time_point m_LastFrameTime;
	const UINT  FramesPerConvergencePercentIncrement = 5;
	float m_ConvergenceIncrement;
	float m_ConvergencePercentPad;

	std::string m_sceneFileDirectory;
	std::unique_ptr<DenoiserPass> m_pDenoiserPass;
	std::unique_ptr<CalculateVariancePass> m_pCalculateVariancePass;
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