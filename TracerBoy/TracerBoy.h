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

class TracerBoy
{
public:
	TracerBoy(ID3D12CommandQueue *pQueue, const std::string &sceneFileName);
	~TracerBoy() { WaitForGPUIdle(); }
	void Render(ID3D12Resource *pBackBuffer);

	void Update(int mouseX, int mouseY, bool keyboardInput[CHAR_MAX], float dt);
private:
	UINT64 SignalFence();
	void ResizeBuffersIfNeeded(ID3D12Resource *pBackBuffer);
	void WaitForGPUIdle();
	void WaitForFenceValue(UINT fenceValue);
	void UpdateConfigConstants(UINT backBufferWidth, UINT backBufferHeight);

	typedef std::pair<ComPtr<ID3D12GraphicsCommandList>, ComPtr<ID3D12CommandAllocator>> CommandListAllocatorPair;
	void AcquireCommandListAllocatorPair(CommandListAllocatorPair &pair);
	UINT ExecuteAndFreeCommandListAllocatorPair(CommandListAllocatorPair &pair);

	void InitializeLocalRootSignature();
	void InitializeTexture(
		const std::wstring& textureName,
		ID3D12GraphicsCommandList& commandList,
		ComPtr<ID3D12Resource>& pResource,
		UINT SRVSlot,
		ComPtr<ID3D12Resource>& pUploadResource);

	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap *pDescriptorHeap, UINT slot);
	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap *pDescriptorHeap, UINT slot);

	void AllocateUploadBuffer(UINT bufferSize, ComPtr<ID3D12Resource>& pBuffer);
	void AllocateBufferWithData(const void *pData, UINT dataSize, ComPtr<ID3D12Resource> &pBuffer);

	ComPtr<ID3D12Device5> m_pDevice;
	ComPtr<ID3D12CommandQueue> m_pCommandQueue;
	ComPtr<ID3D12DescriptorHeap> m_pViewDescriptorHeap;

	ComPtr<ID3D12Resource> m_pBottomLevelAS;
	ComPtr<ID3D12Resource> m_pTopLevelAS;
	ComPtr<ID3D12Resource> m_pConfigConstants;

	ComPtr<ID3D12Resource> m_pEnvironmentMap;
	ComPtr<ID3D12Resource> m_pMaterialList;

	std::vector<ComPtr<ID3D12Resource>> m_pBuffers;

	const DXGI_FORMAT RayTracingOutputFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
	enum OutputUAVs
	{
		PathTracerOutputUAV0 = 0,
		PathTracerOutputUAV1,
		NumPathTracerOutputUAVs
	};
	static const UINT MaxActiveFrames = NumPathTracerOutputUAVs;

	UINT m_FrameFence[MaxActiveFrames];

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
		OutputUAV,
		AccelerationStructureRootSRV,
		RandSeedRootSRV,
		MaterialBufferSRV,
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

	std::deque<std::pair<CommandListAllocatorPair, UINT64>> FreedCommandListAllocatorPairs;

	UINT32 m_mouseX, m_mouseY;
	UINT m_FramesRendered;
	bool m_bInvalidateHistory;

	ComPtr<ID3D12Fence> m_pFence;
	UINT64 m_SignalValue;

	ComPtr<ID3D12Resource> m_pPostProcessOutput;

	enum ViewDescriptorHeapSlots
	{
		PostProcessOutputUAV = 0,
		EnvironmentMapSRVSlot,
		PathTracerOutputSRVBaseSlot,
		PathTracerOutputSRVLastSlot = PathTracerOutputSRVBaseSlot + OutputUAVs::NumPathTracerOutputUAVs - 1,
		PathTracerOutputUAVBaseSlot,
		PathTracerOutputUAVLastSlot = PathTracerOutputUAVBaseSlot + OutputUAVs::NumPathTracerOutputUAVs - 1,
		NumReservedViewSlots,
		NumTotalViews = 1024
	};

	Camera m_camera;

	std::string m_sceneFileDirectory;
};