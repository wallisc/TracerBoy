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
	void UpdateConfigConstants(UINT backBufferWidth, UINT backBufferHeight);

	typedef std::pair<CComPtr<ID3D12GraphicsCommandList>, CComPtr<ID3D12CommandAllocator>> CommandListAllocatorPair;
	void AcquireCommandListAllocatorPair(CommandListAllocatorPair &pair);
	void ExecuteAndFreeCommandListAllocatorPair(CommandListAllocatorPair &pair);

	void InitializeLocalRootSignature();

	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap *pDescriptorHeap, UINT slot);
	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap *pDescriptorHeap, UINT slot);

	void AllocateUploadBuffer(UINT bufferSize, CComPtr<ID3D12Resource>& pBuffer);
	void AllocateBufferWithData(void *pData, UINT dataSize, CComPtr<ID3D12Resource> &pBuffer);

	CComPtr<ID3D12Device5> m_pDevice;
	CComPtr<ID3D12CommandQueue> m_pCommandQueue;
	CComPtr<ID3D12DescriptorHeap> m_pViewDescriptorHeap;

	CComPtr<ID3D12Resource> m_pBottomLevelAS;
	CComPtr<ID3D12Resource> m_pTopLevelAS;
	CComPtr<ID3D12Resource> m_pConfigConstants;

	CComPtr<ID3D12Resource> m_pEnvironmentMap;

	std::vector<CComPtr<ID3D12Resource>> m_pBuffers;

	const DXGI_FORMAT RayTracingOutputFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
	enum OutputUAVs
	{
		PathTracerOutputUAV0 = 0,
		PathTracerOutputUAV1,
		NumPathTracerOutputUAVs
	};
	CComPtr<ID3D12Resource> m_pAccumulatedPathTracerOutput[OutputUAVs::NumPathTracerOutputUAVs];
	UINT8 m_ActivePathTraceOutputIndex;

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
		ConfigConstants,
		EnvironmentMapSRV,
		LastFrameSRV,
		OutputUAV,
		AccelerationStructureRootSRV,
		NumRayTracingParameters
	};
	
	CComPtr<ID3D12RootSignature> m_pLocalRootSignature;

	CComPtr<ID3D12RootSignature> m_pRayTracingRootSignature;
	CComPtr<ID3D12StateObject> m_pRayTracingStateObject;

	CComPtr<ID3D12Resource> m_pRayGenShaderTable;
	CComPtr<ID3D12Resource> m_pHitGroupShaderTable;
	CComPtr<ID3D12Resource> m_pMissShaderTable;


	enum PostProcessRootSignatureParameters
	{
		InputTexture = 0,
		OutputTexture,
		Constants,
		NumParameters
	};
	CComPtr<ID3D12RootSignature> m_pPostProcessRootSignature;
	CComPtr<ID3D12PipelineState> m_pPostProcessPSO;

	std::deque<std::pair<CommandListAllocatorPair, UINT64>> FreedCommandListAllocatorPairs;

	UINT32 m_mouseX, m_mouseY;
	UINT m_FramesRendered;
	bool m_bInvalidateHistory;

	CComPtr<ID3D12Fence> m_pFence;
	UINT64 m_SignalValue;

	CComPtr<ID3D12Resource> m_pPostProcessOutput;

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
};