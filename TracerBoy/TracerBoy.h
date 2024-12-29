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

struct ControllerState
{
	float m_RightStickX;
	float m_RightStickY;
	float m_RightTrigger;
	float m_LeftStickX;
	float m_LeftStickY;
	float m_LeftTrigger;
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
class TemporalAccumulationPass;

enum SceneLoadState
{
	LoadingPBRT,
	LoadingD3D12OnCPU,
	RecordingCommandListWork,
	WaitingOnGPU,
	Finished
};

struct SceneLoadStatus
{
	SceneLoadState State;
	UINT InstancesLoaded;
	UINT TotalInstances;
};

struct MaterialTracker
{
	bool Exists(pbrt::Material* pMaterial)
	{
		const auto& iter = MaterialNameToIndex.find(pMaterial);
		return iter != MaterialNameToIndex.end();
	}

	UINT GetMaterial(pbrt::Material* pMaterial)
	{
		VERIFY(Exists(pMaterial));
		return MaterialNameToIndex[pMaterial];
	}

	UINT AddMaterial(pbrt::Material* pMaterial, const Material m)
	{
		UINT materialIndex = MaterialList.size();
		MaterialNameToIndex[pMaterial] = materialIndex;
		MaterialList.push_back(m);
		MaterialName.push_back(pMaterial->name);
		return materialIndex;
	}

	std::vector<Material> MaterialList;
	std::vector<std::string> MaterialName;
	std::unordered_map<pbrt::Material*, UINT> MaterialNameToIndex;
};

class TracerBoy
{
public:
		
	TracerBoy(ID3D12CommandQueue *pQueue);

	void LoadScene(
		ID3D12GraphicsCommandList& commandList, 
		const std::string& sceneFileName, 
		std::vector<ComPtr<ID3D12Resource>> &resourcesToDelete,
		SceneLoadStatus &sceneLoadStatus,
		std::mutex &sceneLoadStatusUpdateLock);

	enum class OutputType
	{
		Lit,
		Albedo,
		Normals,
		Depth,
		MotionVectors,
		Luminance,
		LuminanceVariance,
		LivePixels,
		LiveWaves,
		Heatmap
	};

	enum class RenderMode
	{
		Unbiased,
		RealTime,
		NumModes
	};

	enum class FilterType : uint
	{
		Box,
		Triangle,
		Gaussian
	};


	enum class TonemapType : uint
	{
		ACES,
		Reinhard,
		Clamp,
		Uncharted,
		KhronosPBRNeutral,
		AGX,
		AGXPunchy,
		GT
	};

	struct CameraOutputSettings
	{
		float m_FocalDistance;
		float m_DOFFocalDistance;
		float m_ApertureWidth;
		
		uint m_FilterType;
		float m_FilterWidth;
	};

	struct PostProcessSettings
	{
		float m_ExposureMultiplier;
		bool m_bEnableGammaCorrection;
		bool m_bEnableFSR;
		bool m_bEnableAutoExposure;

		TonemapType m_TonemapType;
#if USE_XESS
		bool m_bEnableXeSS;
#endif
#if USE_DLSS
		bool m_bEnableDLSS;
#endif
#if USE_DML
		bool m_bEnableDirectMLSuperSampling;
		int m_LayerToDebug;
#endif
#if USE_OIDN
		bool m_bEnableOpenImageDenoise;
		int m_LayerToDebug;
		int m_SliceToDebug;
#endif
	};

	struct PerformanceSettings
	{
		int m_SampleTarget;
		float m_VarianceMultiplier;
		float m_TargetFrameRate;
		float m_ConvergencePercentage;
		bool m_bEnableNextEventEstimation;
		bool m_bEnableSamplingImportanceResampling;
		bool m_bEnableBlueNoise;
		bool m_bEnableInlineRaytracing;
		bool m_bEnableExecuteIndirect;
		int m_OccupancyMultiplier;
		int m_MaxBounces;
	};

	struct DebugSettings 		
	{
		int m_SampleLimit;
		float m_TimeLimitInSeconds;
		float m_VarianceMultiplier;
		float m_DebugValue;
		float m_DebugValue2;
		bool m_bVisualizeRays;
		float m_VisualizeRayWidth;
		float m_VisualizeRayDepth;
		int m_VisualizeRayCount;
	};


	struct OutputSettings
	{
		OutputType m_OutputType;

		bool m_EnableNormalMaps;
		RenderMode m_renderMode;

		DebugSettings m_debugSettings;
		PostProcessSettings m_postProcessSettings;
		CameraOutputSettings m_cameraSettings;
		DenoiserSettings m_denoiserSettings;
		PerformanceSettings m_performanceSettings;
	};

	static OutputSettings GetDefaultOutputSettings()
	{
		OutputSettings outputSettings;
		outputSettings.m_OutputType = OutputType::Lit;
		outputSettings.m_EnableNormalMaps = false;
		outputSettings.m_renderMode = RenderMode::Unbiased;

		DebugSettings &debugSettings = outputSettings.m_debugSettings;
		debugSettings.m_VarianceMultiplier = 1.0f;
		debugSettings.m_SampleLimit = 0;
		debugSettings.m_TimeLimitInSeconds = 0.0f;
		debugSettings.m_DebugValue = 1.0f;
		debugSettings.m_DebugValue2 = 1.0f;
		debugSettings.m_bVisualizeRays = false;
		debugSettings.m_VisualizeRayWidth = 0.01f;
		debugSettings.m_VisualizeRayCount = 0;
		debugSettings.m_VisualizeRayDepth = 10.0f;

		PostProcessSettings& postProcessSettings = outputSettings.m_postProcessSettings;
		postProcessSettings.m_ExposureMultiplier = 1.0f;
		postProcessSettings.m_TonemapType = TonemapType::AGXPunchy;
		postProcessSettings.m_bEnableGammaCorrection = true;
		postProcessSettings.m_bEnableFSR = false;
		postProcessSettings.m_bEnableAutoExposure = true;
#if USE_XESS
		postProcessSettings.m_bEnableXeSS = false;
#endif
#if USE_DLSS
		postProcessSettings.m_bEnableDLSS = false;
#endif
#if USE_DML
		postProcessSettings.m_bEnableDirectMLSuperSampling = true;
		postProcessSettings.m_LayerToDebug = -1;
#endif
#if USE_OIDN
		postProcessSettings.m_bEnableOpenImageDenoise = true;
		postProcessSettings.m_LayerToDebug = -1;
		postProcessSettings.m_SliceToDebug = 0;
#endif

		CameraOutputSettings& cameraSettings = outputSettings.m_cameraSettings;
		cameraSettings.m_FocalDistance = 3.0f;
		cameraSettings.m_DOFFocalDistance = 0.0f;
		cameraSettings.m_ApertureWidth = 0.075f;
		cameraSettings.m_FilterType = (uint)FilterType::Box;
		cameraSettings.m_FilterWidth = 1.0f;

		DenoiserSettings &denoiserSettings = outputSettings.m_denoiserSettings;
		denoiserSettings.m_bEnabled = true;
		denoiserSettings.m_intersectPositionWeightingMultiplier = 1.0f;
		denoiserSettings.m_normalWeightingExponential = 128.0f;
		denoiserSettings.m_luminanceWeightingMultiplier = 4.0f;
		denoiserSettings.m_waveletIterations = 5;
		denoiserSettings.m_fireflyClampValue = 0.0f;
		denoiserSettings.m_maxZ = 10000.0f;

		PerformanceSettings& performanceSettings = outputSettings.m_performanceSettings;
		performanceSettings.m_SampleTarget = 256;
		performanceSettings.m_VarianceMultiplier = 1.0f;
		performanceSettings.m_TargetFrameRate = 0.0f;
		performanceSettings.m_ConvergencePercentage = 0.001;
		performanceSettings.m_bEnableBlueNoise = true;
		performanceSettings.m_bEnableNextEventEstimation = true;
		performanceSettings.m_bEnableSamplingImportanceResampling = false;
		performanceSettings.m_bEnableInlineRaytracing = true;
		performanceSettings.m_bEnableExecuteIndirect = false;
		performanceSettings.m_OccupancyMultiplier = 10;
		performanceSettings.m_MaxBounces = 6;

		return outputSettings;
	}

	struct ReadbackStats
	{
		UINT ActiveWaves;
		UINT ActivePixels;
		float SelectedPixelDistance;
		int SelectedMaterialID;
	};

	bool RequiresGPUFlush(const OutputSettings& outputSettings);

	void Render(ID3D12GraphicsCommandList &commandList, ID3D12Resource *pBackBuffer, ID3D12Resource *pReadbackStats, const OutputSettings &outputSettings);

	// GPU must be flushed before calling this
	void RecompileShaders();

	bool IsMaterialIDValid(int MaterialID) const;
	const Material* GetMaterial(int MaterialID, const std::string **ppMaterialName = nullptr) const;
	void SetMaterial(int MaterialID, Material material);

	UINT GetNumberOfSamplesSinceLastInvalidate() 
	{
		return m_SamplesRendered;
	}

	struct CameraSettings
	{
		float m_movementSpeed;
		bool m_ignoreMouse;
	};
	void Update(int mouseX, int mouseY, bool keyboardInput[CHAR_MAX], float dt, const ControllerState &controllerState, const CameraSettings &cameraSettings);
	void SelectPixel(int x, int y)
	{
		m_selectedPixelX = x;
		m_selectedPixelY = y;
		m_bClearVisualizationRays = true;
	}

	friend class TextureAllocator;
private:
	void UpdateOutputSettings(const OutputSettings& outputSettings);
	D3D12_GPU_DESCRIPTOR_HANDLE GetOutputSRV(OutputType outputType);

	void ResizeBuffersIfNeeded(ID3D12GraphicsCommandList& commandList, ID3D12Resource *pBackBuffer);
	void UpdateConfigConstants(UINT backBufferWidth, UINT backBufferHeight);

	void UpdateMaterialBuffer(ID3D12GraphicsCommandList& commandList);

	void InvalidateHistory(bool bForceRealTimeInvalidate = false);

	UINT GetPathTracerOutputIndex();
	ID3D12Resource *GetPathTracerOutput();
	UINT GetPathTracerOutputUAV();
	UINT GetPathTracerOutputSRV();
	UINT GetPreviousFramePathTracerOutputIndex();
	UINT GetPreviousFramePathTracerOutputSRV();
	UINT GetWorldPositionSRV();
	UINT GetPreviousFrameWorldPositionSRV();


	void InitializeLocalRootSignature();
	void InitializeTexture(
		const std::wstring& textureName,
		ID3D12GraphicsCommandList& commandList,
		ComPtr<ID3D12Resource>& pResource,
		UINT SRVSlot,
		ComPtr<ID3D12Resource>& pUploadResource,
		bool bIsInternalAsset = false,
		bool *bHasAlpha = nullptr);

	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(UINT slot);
	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(UINT slot);

	D3D12_CPU_DESCRIPTOR_HANDLE GetNonShaderVisibleCPUDescriptorHandle(UINT slot);

	void AllocateBuffer(UINT bufferSize, ComPtr<ID3D12Resource>& pBuffer, D3D12_HEAP_TYPE HeapType = D3D12_HEAP_TYPE_DEFAULT);
	void AllocateUploadBuffer(UINT bufferSize, ComPtr<ID3D12Resource>& pBuffer);
	void AllocateBufferWithData(const void *pData, UINT dataSize, ComPtr<ID3D12Resource> &pBuffer);
	void AllocateBufferWithData(
		ID3D12GraphicsCommandList& CommandList, 
		const void *pData, UINT dataSize, 
		ComPtr<ID3D12Resource> &pBuffer, 
		ComPtr<ID3D12Resource> &pUploadBuffer);

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
#if SUPPORT_SW_RAYTRACING
	ComPtr<ID3D12RaytracingFallbackDevice> m_fallbackDevice;
#endif

	float m_downscaleFactor = 1.0;

	bool EmulateRaytracing() 
	{
		return !m_bSupportsHardwareRaytracing; 
	}

	bool m_bSupportsHardwareRaytracing;
	bool m_bSupportsInlineRaytracing;

	std::vector<D3D12_GPU_VIRTUAL_ADDRESS> m_pBottomLevelASList;
	ComPtr<ID3D12Resource> m_pTopLevelAS;
	ComPtr<ID3D12Resource> m_pConfigConstants;


	ComPtr<ID3D12Resource> m_pLightList;
	UINT m_LightCount;

	ComPtr<ID3D12Resource> m_pEnvironmentMap;
	pbrt::math::mat3f m_EnvironmentMapTransform;
	pbrt::math::vec3f m_EnvironmentMapColorScale;

	UINT m_MinWaveAmount;

	ComPtr<ID3D12Resource> m_pStatsBuffer;
	
	ComPtr<ID3D12Resource> m_pVisualizationRayBuffer;
	UINT GetVisualizationRayBufferSize() const;

	ComPtr<ID3D12PipelineState> m_pVisualizeRaysPSO;
	ComPtr<ID3D12RootSignature> m_pVisualizeRaysRootSignature;

	enum VisualizeRaysRootSignatureParameters
	{
		VisualizeRaysInputRays = 0,
		VisualizeRaysWorldPosition,
		VisualizeRaysConstantBuffer,
		VisualizeRaysOutputColor,
		VisualizeRaysNumParameters
	};

	// We keep the material upload heap in memory so that we can do 
	// live updates of the material list without needing to
	// manage the lifetime of the upload heap
	ComPtr<ID3D12Resource> m_pUploadMaterialList;
	ComPtr<ID3D12Resource> m_pMaterialList;

	ComPtr<ID3D12Resource> m_pTextureDataList;

	std::vector<ComPtr<ID3D12Resource>> m_pBuffers;
	std::vector<ComPtr<ID3D12Resource>> m_pTextures;

	const DXGI_FORMAT RayTracingOutputFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
	static const UINT MaxActiveFrames = 2;

	ComPtr<ID3D12Resource> m_pPathTracerOutput[MaxActiveFrames];
	ComPtr<ID3D12Resource> m_pJitteredAccumulatedPathTracerOutput;
	ComPtr<ID3D12Resource> m_pComposittedOutput;
	UINT8 m_ActiveFrameIndex;

	enum TAAUpscaler
	{
		Native,
		FSR,
		XeSS,
		DLSS,
		DirectML,
		OIDN,
		None
	};

	TAAUpscaler GetSelectedUpscaler(const OutputSettings &outputSettings, bool& bNeedsMotionVectors, bool& bNeedsFixedPixelOffset);

	enum LocalRayTracingRootSignatureParameters
	{
		GeometryIndexRootConstant = 0,
		NumLocalRayTracingParameters
	};

	enum RayTracingRootSignatureParameters
	{
		PerFrameConstantsParam = 0,
		ConfigConstantsParam,
		AOVDescriptorTable,
		OutputUAV,
		JitteredOutputUAV,
		AccelerationStructureRootSRV,
		SceneDescriptorTable,
		SystemTexturesDescriptorTable,
		ImageTextureTable,
#if SUPPORT_VOLUMES
		VolumeSRVParam,
#endif
		LightList,
		ShaderTable,
		StatsBuffer,
		VisualizationRayBuffer,
		PreviousFrameOutput,
		NumRayTracingParameters
	};

	ComPtr<ID3D12RootSignature> m_pLocalRootSignature;

	ComPtr<ID3D12RootSignature> m_pRayTracingRootSignature;
	ComPtr<ID3D12StateObject> m_pRayTracingStateObject;
	ComPtr<ID3D12PipelineState> m_pRayTracingPSO;
	ComPtr<ID3D12PipelineState> m_pSoftwareRayTracingPSO;

	ComPtr<ID3D12Resource> m_pRayGenShaderTable;
	ComPtr<ID3D12Resource> m_pHitGroupShaderTable;
	ComPtr<ID3D12Resource> m_pMissShaderTable;


	enum PostProcessRootSignatureParameters
	{
		InputTexture = 0,
		AuxTexture,
		AveragedLuminance,
		OutputTexture,
		Constants,
		NumParameters
	};
	ComPtr<ID3D12RootSignature> m_pPostProcessRootSignature;
	ComPtr<ID3D12PipelineState> m_pPostProcessPSO;

	enum CompositeAlbedoRootSignatureParameters
	{
		CompositeAlbedoInputAlbedo= 0,
		CompositeAlbedoIndirectLighting,
		CompositeAlbedoEmissive,
		CompositeAlbedoOutputTexture,
		CompositeAlbedoNumParameters
	};
	ComPtr<ID3D12RootSignature> m_pCompositeAlbedoRootSignature;
	ComPtr<ID3D12PipelineState> m_pCompositeAlbedoPSO;

	enum GenerateHistogramRootSignatureParameters
	{
		GenerateHistogramConstantsParam = 0,
		GenerateHistogramInputTextureSRV,
		GenerateHistogramLuminanceHistogramUAV,
		GenerateHistogramNumParameters
	};
	ComPtr<ID3D12RootSignature> m_pGenerateHistogramRootSignature;
	ComPtr<ID3D12PipelineState> m_pGenerateHistogramPSO;

	enum AverageLuminanceRootSignatureParameters
	{
		AverageLuminanceConstantsParam = 0,
		AverageLuminanceHistogramSRV,
		AverageLuminanceOutputUAV,
		AverageLuminanceNumParameters
	};
	ComPtr<ID3D12RootSignature> m_pAverageLuminanceRootSignature;
	ComPtr<ID3D12PipelineState> m_pAverageLuminancePSO;

	UINT32 m_mouseX, m_mouseY;
	UINT32 m_selectedPixelX, m_selectedPixelY;
	UINT m_SamplesRendered;
	bool m_bInvalidateHistory;
	bool m_bClearVisualizationRays = true;
	bool m_bUpdateMaterialList = false;

	ComPtr<ID3D12Resource> m_pLuminanceHistogram;
	ComPtr<ID3D12Resource> m_pAveragedLuminance;

	ComPtr<ID3D12Resource> m_pPostProcessOutput;
	ComPtr<ID3D12Resource> m_pMotionVectors;
	PassResource m_pUpscaleOutput;
	PassResource m_pUpscaleItermediateOutput;
	PassResource m_pDenoiserBuffers[2];

	ComPtr<ID3D12Resource> m_pAOVNormals;
	ComPtr<ID3D12Resource> m_pAOVCustomOutput;
	ComPtr<ID3D12Resource> m_pAOVWorldPosition[2];
	ComPtr<ID3D12Resource> m_pAOVDepth;
	ComPtr<ID3D12Resource> m_pAOVEmissive;
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
		PostProcessOutputSRV,
		TemporalOutputBaseSRV,
		TemporalOutputLastSRV,
		TemporalOutputBaseUAV,
		TemporalOutputLastUAV,
		IndirectLightingTemporalOutputBaseSRV,
		IndirectLightingTemporalOutputLastSRV,
		IndirectLightingTemporalOutputBaseUAV,
		IndirectLightingTemporalOutputLastUAV,
		MomentTextureBaseSRV,
		MomentTextureLastSRV,
		MomentTextureBaseUAV,
		MomentTextureLastUAV,
		DenoiserOuputBaseSRV,
		DenoiserOuputLastSRV,
		DenoiserOutputBaseUAV,
		DenoiserOutputLastUAV,
		SystemTexturesBaseSlot,
		BlueNoise0SRVSlot = SystemTexturesBaseSlot,
		BlueNoise1SRVSlot,
		SystemTexturesLastSlot = BlueNoise1SRVSlot,
		SceneDescriptorsBaseSlot,
		EnvironmentMapSRVSlot = SceneDescriptorsBaseSlot,
		MaterialListSRV,
		TextureDataSRV,
		SceneDescriptorsLastSlot = TextureDataSRV,
		AOVBaseUAVSlot,
		AOVNormalsUAV = AOVBaseUAVSlot,
		AOVWorldPosition0UAV,
		AOVWorldPosition1UAV,
		AOVCustomOutputUAV,
		AOVDepthUAV,
		AOVEmissiveUAV,
		AOVLastUAVSlot = AOVEmissiveUAV,
		AOVBaseSRVSlot,
		AOVNormalsSRV = AOVBaseSRVSlot,
		AOVWorldPosition0SRV,
		AOVWorldPosition1SRV,
		AOVCustomOutputSRV,
		AOVDepthSRV,
		AOVEmissiveSRV,
		AOVLastSRVSlot = AOVEmissiveSRV,
		LuminanceVarianceSRV,
		LuminanceVarianceUAV,
		VolumeSRVSlot,
		PathTracerOutputSRV0,
		PathTracerOutputSRV1,
		PathTracerOutputUAV0,
		PathTracerOutputUAV1,
		JitteredPathTracerOutputSRV,
		JitteredPathTracerOutputUAV,
		ComposittedOutputUAV,
		ComposittedOutputSRV,
		UpscaledBufferUAV,
		UpscaledIntermediateBufferUAV,
		UpscaledIntermediateBufferSRV,
		RayIndexBufferUAV,
		IndirectArgsUAV,
		StatsBufferUAV,
		VisualizationRayBufferUAV,
		MotionVectorsSRV,
		MotionVectorsUAV,
		LuminanceHistogramSRV,
		LuminanceHistogramUAV,
		AveragedLuminanceSRV,
		AveragedLuminanceUAV,
#if SUPPORT_SW_RAYTRACING
		TopLevelAccelerationStructureUAV,
#endif
		NumReservedViewSlots,
		NumTotalViews = 1024 * 960,
		NumAOVTextures = AOVLastUAVSlot - AOVBaseUAVSlot + 1,
		NumSystemTextures = SystemTexturesLastSlot - SystemTexturesBaseSlot + 1,
		NumSceneDescriptors = SceneDescriptorsLastSlot - SceneDescriptorsBaseSlot + 1

#if USE_DML || USE_OIDN
		, DMLBaseSlot = NumTotalViews
#endif
	};

	MaterialTracker m_MaterialTracker;

	Vector3 m_volumeMax;
	Vector3 m_volumeMin;

	OutputSettings m_CachedOutputSettings;
	Camera m_camera;
	Camera m_prevFrameCamera;
	bool m_flipTextureUVs;

	std::chrono::steady_clock::time_point m_RenderStartTime;

	std::chrono::steady_clock::time_point m_LastFrameTime;
	const UINT  FramesPerConvergencePercentIncrement = 5;
	float m_ConvergenceIncrement;
	float m_ConvergencePercentPad;

	PassResource m_pFinalTemporalOutput[MaxActiveFrames];
	PassResource m_pIndirectLightingTemporalOutput[MaxActiveFrames];
	PassResource m_pMomentBuffer[MaxActiveFrames];


	std::string m_sceneFileDirectory;
	std::unique_ptr<DenoiserPass> m_pDenoiserPass;
	std::unique_ptr<TemporalAccumulationPass> m_pTemporalAccumulationPass;
	std::unique_ptr<GenerateMotionVectorsPass> m_pGenerateMotionVectorsPass;
	std::unique_ptr<FidelityFXSuperResolutionPass> m_pFidelityFXSuperResolutionPass;
#if USE_DLSS
	std::unique_ptr<DeepLearningSuperSamplingPass> m_pDLSSPass;
#endif
#if USE_XESS
	std::unique_ptr<XeSuperSamplingPass> m_pXeSSPass;
#endif
#if USE_DML
	std::unique_ptr<DirectMLSuperResolutionPass> m_pDirectMLSuperResolutionPass;
#endif
#if USE_OIDN
	std::unique_ptr<OpenImageDenoise> m_pOpenImageDenoise;		
#endif
};

class TextureAllocator
{
public:
	TextureAllocator(TracerBoy& tracerBoy, ID3D12GraphicsCommandList& CommandList) :
		m_tracerboy(tracerBoy),
		m_pCommandList(&CommandList)
	{}

	UINT CreateTexture(pbrt::Texture::SP& pPbrtTexture, bool bGammaCorrect = false, bool *bHasAlpha = nullptr);
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