#pragma once

#if USE_OIDN
#include "DirectML.h"
#include <span>

struct PassResource;


typedef std::vector<float> WeightsType;
typedef std::map<std::string, WeightsType> WeightMapType;

class Tensor;

// Let DirectML manage the data in the weight tensors. This can be faster on some hardware.
#define DML_MANAGED_WEIGHTS 0

class OpenImageDenoise
{
public:
	OpenImageDenoise(ID3D12Device& device);

	UINT GetRequiredDescriptorCount() { return m_RequiredDescriptors; }

	void OnResize(
		ID3D12GraphicsCommandList& commandList,
		CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHeapCPUBase,
		CD3DX12_GPU_DESCRIPTOR_HANDLE descriptorHeapGPUBase,
		UINT descriptorSize,
		UINT Width,
		UINT Height,
		float& OutDownscaleFactor);

	static const UINT cDisableLayerDebugging = UINT_MAX;
	D3D12_GPU_DESCRIPTOR_HANDLE Run(ID3D12GraphicsCommandList& commandList,
		PassResource OutputBuffer,
		D3D12_GPU_DESCRIPTOR_HANDLE InputTexture,
		UINT inputWidth,
		UINT inputHeight,
		UINT convolutionLayerToDebug = cDisableLayerDebugging,
		UINT convolutionSliceToDebug = 0);

	enum class TensorLayout
	{
		Default,
		NHWC
	};

	class DirectMLPass
	{
	public:
		IDMLCompiledOperator *GetOperator() { return m_pOperator.Get(); }
		IDMLBindingTable *GetBindingTable() { return m_pBindingTable.Get(); }

		ComPtr<IDMLCompiledOperator> m_pOperator;
		ComPtr<IDMLBindingTable> m_pBindingTable;

		UINT m_InputWidth, m_InputHeight, m_InputChannelDepth;
		UINT m_OutputWidth, m_OutputHeight, m_OutputChannelDepth;
		D3D12_GPU_DESCRIPTOR_HANDLE m_OutputSRV;
	};

	class ConvolutionPass : public DirectMLPass
	{
	public:		
		Microsoft::WRL::ComPtr<ID3D12Resource> m_pFilterWeightResource;
		Microsoft::WRL::ComPtr<ID3D12Resource> m_pBiasWeightResource;
	};

	class PoolingPass : public DirectMLPass
	{
	public:
		Microsoft::WRL::ComPtr<ID3D12Resource> m_pOutputResource;
	};

private:
	ConvolutionPass CreateConvolutionLayer(
		ID3D12GraphicsCommandList& commandList,
		const Tensor& weights,
		const Tensor& bias,
		_In_reads_(4) const uint32_t* inputSizes,
		bool useBiasAndActivation,
		_Inout_updates_(1) uint64_t* inputBufferRequiredSize,
		_Inout_updates_(1) uint64_t* outputBufferRequiredSize,
		_Out_writes_(4) uint32_t* outputSizesOut,
		_Out_writes_(1) IDMLCompiledOperator** compiledOpOut);

	void CreateAdditionLayer(
		_In_reads_(4) const uint32_t* inputSizes,
		_Out_writes_(1) IDMLCompiledOperator** compiledOpOut);

	PoolingPass CreatePoolingLayer(
		_In_reads_(4) const uint32_t* inputSizes,
		_Out_writes_(1) IDMLCompiledOperator** compiledOpOut);

	void CreateUpsampleLayer(
		_In_reads_(4) const uint32_t* inputSizes,
		_Inout_updates_(1) uint64_t* inputBufferRequiredSize,
		_Inout_updates_(1) uint64_t* outputBufferRequiredSize,
		_Out_writes_(4) uint32_t* outputSizesOut,
		_Out_writes_(1) IDMLCompiledOperator** compiledOpOut);

	void CreateWeightTensors(
		ID3D12GraphicsCommandList& commandList,
		const Tensor& weights,
		const Tensor& bias,
		std::span<const uint32_t> filterSizes,
		_Out_writes_(1) ID3D12Resource** filterWeightResourceOut,
		_Out_writes_opt_(1) ID3D12Resource** biasWeightResourceOut);
	
	void CreateWeightResource(
		_In_reads_(4) const uint32_t* tensorSizes,
		_Out_writes_(1) ID3D12Resource** d3dResourceOut);

	// Model layer sizes and indices
	static const size_t                             c_numUpsampleLayers = 4;
	//static const size_t                             c_numConvLayers = 16;
	static const size_t                             c_numConvLayers = 2;
	static const size_t                             c_numJoinLayers = 4;
	//static const size_t                             c_numPoolingLayers = 4;
	static const size_t                             c_numPoolingLayers = 1;
	static const size_t                             c_numIntermediateBuffers = 2;

	// Hard-coded so that we don't need to recreate the descriptor heap on window resize
	const UINT m_RequiredDescriptors = 256;

	enum OpTypes : uint32_t
	{
		e_opUpsample,
		e_opConv,
		e_opAdd,
		e_opPooling,
		e_opCount
	};

	void ResetDescriptorTable(
		CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHeapCPUBase,
		CD3DX12_GPU_DESCRIPTOR_HANDLE descriptorHeapGPUBase,
		UINT descriptorSize)
	{
		m_descriptorHeapCPUBase = descriptorHeapCPUBase;
		m_descriptorHeapGPUBase = descriptorHeapGPUBase;
		m_descriptorSize = descriptorSize;
		m_descriptorHeapIndex = 0;
	}

	struct DescriptorHeapEntry
	{
		D3D12_GPU_DESCRIPTOR_HANDLE m_gpuHandle;
		D3D12_CPU_DESCRIPTOR_HANDLE m_cpuHandle;
	};

	DescriptorHeapEntry AllocateDescriptor(uint NumDescriptors = 1)
	{
		DescriptorHeapEntry entry;
		entry.m_cpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_descriptorHeapCPUBase, m_descriptorHeapIndex, m_descriptorSize);
		entry.m_gpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_descriptorHeapGPUBase, m_descriptorHeapIndex, m_descriptorSize);
		m_descriptorHeapIndex += NumDescriptors;

		VERIFY(m_descriptorHeapIndex <= m_RequiredDescriptors);

		return entry;
	}
		

	CD3DX12_CPU_DESCRIPTOR_HANDLE m_descriptorHeapCPUBase;
	CD3DX12_GPU_DESCRIPTOR_HANDLE m_descriptorHeapGPUBase;
	UINT m_descriptorSize;
	UINT m_descriptorHeapIndex;

	ComPtr<IDMLDevice> m_pDMLDevice;

	ComPtr<IDMLCompiledOperator>    m_dmlUpsampleOps[c_numUpsampleLayers];
	ComPtr<IDMLBindingTable>        m_dmlUpsampleBindings[c_numUpsampleLayers];
	ComPtr<IDMLCompiledOperator>    m_dmlConvOps[c_numConvLayers];
	ComPtr<IDMLCompiledOperator>    m_dmlPoolingOps[c_numPoolingLayers];
	ComPtr<IDMLBindingTable>        m_dmlPoolingBindings[c_numPoolingLayers];
	ComPtr<IDMLCompiledOperator>    m_dmlJoinOps[c_numJoinLayers];
	ComPtr<IDMLBindingTable>        m_dmlJoinBindings[c_numJoinLayers];

	ConvolutionPass					m_ConvolutionPasses[c_numConvLayers];
	PoolingPass						m_PoolingPass[c_numConvLayers];
	std::vector<DirectMLPass*>		m_DMLPasses;

	Microsoft::WRL::ComPtr<ID3D12Resource>          m_modelUpsamplePersistentResources[c_numUpsampleLayers];
	Microsoft::WRL::ComPtr<ID3D12Resource>          m_modelConvPersistentResources[c_numConvLayers];
	Microsoft::WRL::ComPtr<ID3D12Resource>          m_modelPoolingPersistentResources[c_numPoolingLayers];
	Microsoft::WRL::ComPtr<ID3D12Resource>          m_modelAddPersistentResource;

	Microsoft::WRL::ComPtr<ID3D12Resource>          m_modelInitTemporaryResources[e_opCount];
	Microsoft::WRL::ComPtr<ID3D12Resource>          m_modelUpsampleTemporaryResources[c_numUpsampleLayers];
	Microsoft::WRL::ComPtr<ID3D12Resource>          m_modelPoolingTemporaryResources[c_numPoolingLayers];
	Microsoft::WRL::ComPtr<ID3D12Resource>          m_modelConvTemporaryResources[c_numConvLayers];
	Microsoft::WRL::ComPtr<ID3D12Resource>          m_modelAddTemporaryResource;

	Microsoft::WRL::ComPtr<ID3D12Resource>          m_modelInput;
	D3D12_GPU_DESCRIPTOR_HANDLE						m_modelInputUAV;

	Microsoft::WRL::ComPtr<ID3D12Resource>          m_modelOutput;
	D3D12_GPU_DESCRIPTOR_HANDLE						m_modelOutputSRV;
	D3D12_GPU_DESCRIPTOR_HANDLE						m_modelIntermediateSRV[c_numIntermediateBuffers];

	Microsoft::WRL::ComPtr<ID3D12Resource>          m_modelIntermediateResult[c_numIntermediateBuffers];

	TensorLayout m_tensorLayout = TensorLayout::Default;

	ComPtr<IDMLOperatorInitializer> m_dmlOpInitializers[e_opCount];
	ComPtr<IDMLCommandRecorder>     m_pCommandRecorder;
	ID3D12Device& m_device;

	enum DirectMLSuperResolutionRootSignatureParameters
	{
		ConstantsParam = 0,
		Input,
		Output,
		NumRootSignatureParameters
	};

	ComPtr<ID3D12RootSignature> m_pRootSignature;
	ComPtr<ID3D12PipelineState> m_pTensorToImagePSO;

	ComPtr<ID3D12RootSignature> m_pImageToTensorRootSignature;
	ComPtr<ID3D12PipelineState> m_pImageToTensorPSO;

	std::vector<ComPtr<ID3D12Resource>> m_uploadResources;
};
#endif