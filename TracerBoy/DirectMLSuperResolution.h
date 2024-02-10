#pragma once

#if USE_DML
#include "DirectML.h"
#include <span>

struct PassResource;


typedef std::vector<float> WeightsType;
typedef std::map<std::string, WeightsType> WeightMapType;

// Let DirectML manage the data in the weight tensors. This can be faster on some hardware.
#define DML_MANAGED_WEIGHTS 0

class DirectMLSuperResolutionPass
{
public:
	DirectMLSuperResolutionPass(ID3D12Device& device);

	UINT GetRequiredDescriptorCount() { return m_RequiredDescriptors; }

	void OnResize(
		ID3D12GraphicsCommandList& commandList,
		CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHeapCPUBase,
		CD3DX12_GPU_DESCRIPTOR_HANDLE descriptorHeapGPUBase,
		UINT descriptorSize,
		UINT Width,
		UINT Height,
		float& OutDownscaleFactor);

	D3D12_GPU_DESCRIPTOR_HANDLE Run(ID3D12GraphicsCommandList& commandList,
		PassResource OutputBuffer,
		D3D12_GPU_DESCRIPTOR_HANDLE InputTexture,
		UINT inputWidth,
		UINT inputHeight);


	enum class TensorLayout
	{
		Default,
		NHWC
	};
private:
	void CreateConvolutionLayer(
		_In_reads_(4) const uint32_t* inputSizes,
		_In_reads_(4) const uint32_t* filterSizes,
		bool useBiasAndActivation,
		_Inout_updates_(1) uint64_t* inputBufferRequiredSize,
		_Inout_updates_(1) uint64_t* outputBufferRequiredSize,
		_Out_writes_(4) uint32_t* outputSizesOut,
		_Out_writes_(1) IDMLCompiledOperator** compiledOpOut);

	void CreateAdditionLayer(
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
		WeightMapType& weights,
		const char* convLayerName,
		const char* scaleLayerName,
		const char* shiftLayerName,
		std::span<const uint32_t> filterSizes,
		//DirectX::ResourceUploadBatch& uploadBatch,
		_Out_writes_(1) ID3D12Resource** filterWeightResourceOut,
		_Out_writes_opt_(1) ID3D12Resource** biasWeightResourceOut);

	
	void CreateWeightResource(
		_In_reads_(4) const uint32_t* tensorSizes,
		_Out_writes_(1) ID3D12Resource** d3dResourceOut);

	// Model layer sizes and indices
	static const size_t                             c_numUpsampleLayers = 2;
	static const size_t                             c_numConvLayers = 7;
	static const size_t                             c_numIntermediateBuffers = 2;

	// Hard-coded so that we don't need to recreate the descriptor heap on window resize
	const UINT m_RequiredDescriptors = 256;

	enum OpTypes : uint32_t
	{
		e_opUpsample,
		e_opConv,
		e_opAdd,
		e_opCount
	};

	ComPtr<IDMLDevice> m_pDMLDevice;

	ComPtr<IDMLCompiledOperator>    m_dmlUpsampleOps[c_numUpsampleLayers];
	ComPtr<IDMLBindingTable>        m_dmlUpsampleBindings[c_numUpsampleLayers];
	ComPtr<IDMLCompiledOperator>    m_dmlConvOps[c_numConvLayers];
	ComPtr<IDMLBindingTable>        m_dmlConvBindings[c_numConvLayers];
	ComPtr<IDMLCompiledOperator>    m_dmlAddResidualOp;
	ComPtr<IDMLBindingTable>        m_dmlAddResidualBinding;

	Microsoft::WRL::ComPtr<ID3D12Resource>          m_modelUpsamplePersistentResources[c_numUpsampleLayers];
	Microsoft::WRL::ComPtr<ID3D12Resource>          m_modelConvPersistentResources[c_numConvLayers];
	Microsoft::WRL::ComPtr<ID3D12Resource>          m_modelAddPersistentResource;

	Microsoft::WRL::ComPtr<ID3D12Resource>          m_modelInitTemporaryResources[e_opCount];
	Microsoft::WRL::ComPtr<ID3D12Resource>          m_modelUpsampleTemporaryResources[c_numUpsampleLayers];
	Microsoft::WRL::ComPtr<ID3D12Resource>          m_modelConvTemporaryResources[c_numConvLayers];
	Microsoft::WRL::ComPtr<ID3D12Resource>          m_modelAddTemporaryResource;

	Microsoft::WRL::ComPtr<ID3D12Resource>          m_modelConvFilterWeights[c_numConvLayers];
	Microsoft::WRL::ComPtr<ID3D12Resource>          m_modelConvBiasWeights[c_numConvLayers];

	Microsoft::WRL::ComPtr<ID3D12Resource>          m_modelInput;
	D3D12_GPU_DESCRIPTOR_HANDLE						m_modelInputUAV;

	Microsoft::WRL::ComPtr<ID3D12Resource>          m_modelOutput;
	D3D12_GPU_DESCRIPTOR_HANDLE						m_modelOutputSRV;

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