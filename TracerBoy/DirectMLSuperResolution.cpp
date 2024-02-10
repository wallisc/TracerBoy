#include "pch.h"

#if USE_DML

#include "TensorToImageCS.h"
#include "ImageToTensorCS.h"
#include "DirectMLSharedShaderStructs.h"

// Float32 to float16 compressor
// Code from here: https://stackoverflow.com/a/3542975
// Used under the Unlicense: http://choosealicense.com/licenses/unlicense/

class Float16Compressor
{
    union Bits
    {
        float f;
        int32_t si;
        uint32_t ui;
    };

    static int const shift = 13;
    static int const shiftSign = 16;

    static int32_t const infN = 0x7F800000; // flt32 infinity
    static int32_t const maxN = 0x477FE000; // max flt16 normal as a flt32
    static int32_t const minN = 0x38800000; // min flt16 normal as a flt32
    static int32_t const signN = 0x80000000; // flt32 sign bit

    static int32_t const infC = infN >> shift;
    static int32_t const nanN = (infC + 1) << shift; // minimum flt16 nan as a flt32
    static int32_t const maxC = maxN >> shift;
    static int32_t const minC = minN >> shift;
    static int32_t const signC = signN >> shiftSign; // flt16 sign bit

    static int32_t const mulN = 0x52000000; // (1 << 23) / minN
    static int32_t const mulC = 0x33800000; // minN / (1 << (23 - shift))

    static int32_t const subC = 0x003FF; // max flt32 subnormal down shifted
    static int32_t const norC = 0x00400; // min flt32 normal down shifted

    static int32_t const maxD = infC - maxC - 1;
    static int32_t const minD = minC - subC - 1;

public:

    static uint16_t compress(float value)
    {
        Bits v, s;
        v.f = value;
        uint32_t sign = v.si & signN;
        v.si ^= sign;
        sign >>= shiftSign; // logical shift
        s.si = mulN;
        s.si = static_cast<int32_t>(s.f * v.f); // correct subnormals
        v.si ^= (s.si ^ v.si) & -(minN > v.si);
        v.si ^= (infN ^ v.si) & -((infN > v.si) & (v.si > maxN));
        v.si ^= (nanN ^ v.si) & -((nanN > v.si) & (v.si > infN));
        v.ui >>= shift; // logical shift
        v.si ^= ((v.si - maxD) ^ v.si) & -(v.si > maxC);
        v.si ^= ((v.si - minD) ^ v.si) & -(v.si > subC);
        return static_cast<uint16_t>(v.ui | sign);
    }

    static float decompress(uint16_t value)
    {
        Bits v;
        v.ui = value;
        int32_t sign = v.si & signC;
        v.si ^= sign;
        sign <<= shiftSign;
        v.si ^= ((v.si + minD) ^ v.si) & -(v.si > subC);
        v.si ^= ((v.si + maxD) ^ v.si) & -(v.si > maxC);
        Bits s;
        s.si = mulC;
        s.f *= v.si;
        int32_t mask = -(norC > v.si);
        v.si <<= shift;
        v.si ^= (s.si ^ v.si) & mask;
        v.si |= sign;
        return v.f;
    }
};

#include <iostream>
#include <fstream>

namespace
{
    const int c_bufferLength = 256;
}

// Loads weight values from a binary file.
bool LoadWeights(const std::string& fpath, WeightMapType& weightMap)
{
    std::ifstream input(fpath, std::ifstream::binary);
    if (!(input) || !(input.good()) || !(input.is_open()))
    {
        std::cerr << "Unable to open weight file: " << fpath << std::endl;
        return false;
    }

    int32_t count;
    try
    {
        input.read(reinterpret_cast<char*>(&count), 4);
    }
    catch (const std::ifstream::failure&)
    {
        std::cerr << "Invalid weight map file: " << fpath << std::endl;
        return false;
    }
    if (count < 0)
    {
        std::cerr << "Invalid weight map file: " << fpath << std::endl;
        return false;
    }
    std::cout << "Number of weight tensors: " + std::to_string(count) << std::endl;

    uint32_t name_len;
    uint32_t w_len;
    char name_buf[c_bufferLength];

    try
    {
        while (count--)
        {
            input.read(reinterpret_cast<char*>(&name_len), sizeof(uint32_t));
            if (name_len > c_bufferLength - 1)
            {
                std::cerr << "name_len exceeds c_bufferLength: " << name_len
                    << " vs " << c_bufferLength - 1 << std::endl;
                return false;
            }
            input.read(name_buf, name_len);
            name_buf[name_len] = '\0';
            std::string name(name_buf);

            input.read(reinterpret_cast<char*>(&w_len), sizeof(uint32_t));
            weightMap[name] = WeightsType(w_len);
            input.read(reinterpret_cast<char*>(weightMap[name].data()), sizeof(float) * w_len);

            std::cout << "Loaded tensor: " + name + " -> " + std::to_string(w_len) << std::endl;
        }

        input.close();
    }
    catch (const std::ifstream::failure&)
    {
        std::cerr << "Invalid tensor data" << std::endl;
        return false;
    }
    catch (const std::out_of_range&)
    {
        std::cerr << "Invalid tensor format" << std::endl;
        return false;
    }

    return true;
}

UINT64 DMLCalcBufferTensorSize(
    DML_TENSOR_DATA_TYPE dataType,
    UINT dimensionCount,
    _In_reads_(dimensionCount) const UINT* sizes,
    _In_reads_opt_(dimensionCount) const UINT* strides)
{
    UINT elementSizeInBytes = 0;
    switch (dataType)
    {
    case DML_TENSOR_DATA_TYPE_FLOAT32:
    case DML_TENSOR_DATA_TYPE_UINT32:
    case DML_TENSOR_DATA_TYPE_INT32:
        elementSizeInBytes = 4;
        break;

    case DML_TENSOR_DATA_TYPE_FLOAT16:
    case DML_TENSOR_DATA_TYPE_UINT16:
    case DML_TENSOR_DATA_TYPE_INT16:
        elementSizeInBytes = 2;
        break;

    case DML_TENSOR_DATA_TYPE_UINT8:
    case DML_TENSOR_DATA_TYPE_INT8:
        elementSizeInBytes = 1;
        break;

    default:
        return 0; // Invalid data type
    }

    UINT64 minimumImpliedSizeInBytes = 0;
    if (!strides)
    {
        minimumImpliedSizeInBytes = sizes[0];
        for (UINT i = 1; i < dimensionCount; ++i)
        {
            minimumImpliedSizeInBytes *= sizes[i];
        }
        minimumImpliedSizeInBytes *= elementSizeInBytes;
    }
    else
    {
        UINT indexOfLastElement = 0;
        for (UINT i = 0; i < dimensionCount; ++i)
        {
            indexOfLastElement += (sizes[i] - 1) * strides[i];
        }

        minimumImpliedSizeInBytes = (static_cast<UINT64>(indexOfLastElement) + 1) * elementSizeInBytes;
    }

    // Round up to the nearest 4 bytes.
    minimumImpliedSizeInBytes = (minimumImpliedSizeInBytes + 3) & ~3ull;

    return minimumImpliedSizeInBytes;
}

DirectMLSuperResolutionPass::DirectMLSuperResolutionPass(ID3D12Device& device) :
    m_device(device)
{
	const bool bEnableDMLDebugLayer = false;
	VERIFY_HRESULT(DMLCreateDevice(&device,
		bEnableDMLDebugLayer ? DML_CREATE_DEVICE_FLAG_DEBUG : DML_CREATE_DEVICE_FLAG_NONE, 
		IID_GRAPHICS_PPV_ARGS(m_pDMLDevice.ReleaseAndGetAddressOf())));
	
	VERIFY_HRESULT(m_pDMLDevice->CreateCommandRecorder(IID_PPV_ARGS(m_pCommandRecorder.ReleaseAndGetAddressOf())));

    CD3DX12_ROOT_PARAMETER1 Parameters[DirectMLSuperResolutionRootSignatureParameters::NumRootSignatureParameters] = {};
    Parameters[DirectMLSuperResolutionRootSignatureParameters::ConstantsParam].InitAsConstants(sizeof(DirectMLConstants) / sizeof(UINT32), 0);

    CD3DX12_DESCRIPTOR_RANGE1 InputTextureDescriptor;
    InputTextureDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
    Parameters[DirectMLSuperResolutionRootSignatureParameters::Input].InitAsDescriptorTable(1, &InputTextureDescriptor);

    CD3DX12_DESCRIPTOR_RANGE1 OutputUAVDescriptor;
    OutputUAVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
    Parameters[DirectMLSuperResolutionRootSignatureParameters::Output].InitAsDescriptorTable(1, &OutputUAVDescriptor);

    D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
    rootSignatureDesc.pParameters = Parameters;
    rootSignatureDesc.NumParameters = ARRAYSIZE(Parameters);
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC versionedRSDesc(rootSignatureDesc);

    ComPtr<ID3DBlob> pRootSignatureBlob;
    VERIFY_HRESULT(D3D12SerializeVersionedRootSignature(&versionedRSDesc, &pRootSignatureBlob, nullptr));
    VERIFY_HRESULT(device.CreateRootSignature(0, pRootSignatureBlob->GetBufferPointer(), pRootSignatureBlob->GetBufferSize(), IID_GRAPHICS_PPV_ARGS(m_pRootSignature.ReleaseAndGetAddressOf())));
    
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_pRootSignature.Get();

    psoDesc.CS = CD3DX12_SHADER_BYTECODE(g_pImageToTensorCS, sizeof(g_pImageToTensorCS));
    VERIFY_HRESULT(device.CreateComputePipelineState(&psoDesc, IID_GRAPHICS_PPV_ARGS(m_pImageToTensorPSO.ReleaseAndGetAddressOf())));

    psoDesc.CS = CD3DX12_SHADER_BYTECODE(g_pTensorToImageCS, sizeof(g_pTensorToImageCS));
    VERIFY_HRESULT(device.CreateComputePipelineState(&psoDesc, IID_GRAPHICS_PPV_ARGS(m_pTensorToImagePSO.ReleaseAndGetAddressOf())));
}

UINT GetDescriptorCount(size_t numOps, IDMLCompiledOperator** ops, IDMLOperatorInitializer* initializer)
{
    auto bindingProps = initializer->GetBindingProperties();

    UINT requiredDescriptorCount = bindingProps.RequiredDescriptorCount;

    for (size_t i = 0; i < numOps; i++)
    {
        bindingProps = ops[i]->GetBindingProperties();
        requiredDescriptorCount = std::max(requiredDescriptorCount, bindingProps.RequiredDescriptorCount);
    }

    return requiredDescriptorCount;
}

static void BindTempResourceIfNeeded(ID3D12Device& device, DML_BINDING_PROPERTIES& bindingProps, IDMLBindingTable* initBindingTable, ID3D12Resource** tempResource)
{
    if (bindingProps.TemporaryResourceSize > 0)
    {
        D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bindingProps.TemporaryResourceSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        auto heapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        VERIFY_HRESULT(device.CreateCommittedResource(
            &heapDesc,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(tempResource)));

        DML_BUFFER_BINDING tempBuffer = { *tempResource, 0, (*tempResource)->GetDesc().Width };
        DML_BINDING_DESC tempBinding = { DML_BINDING_TYPE_BUFFER, &tempBuffer };
        initBindingTable->BindTemporaryResource(&tempBinding);
    }
}

void DirectMLSuperResolutionPass::OnResize(
    ID3D12GraphicsCommandList& commandList,
    CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHeapCPUBase,
    CD3DX12_GPU_DESCRIPTOR_HANDLE descriptorHeapGPUBase,
    UINT descriptorSize,
    UINT UpscaledWidth,
    UINT UpscaledHeight,
    float& OutDownscaleFactor)
{
    OutDownscaleFactor = 0.5f;
    UINT Width = UpscaledWidth * OutDownscaleFactor;
    UINT Height = UpscaledHeight * OutDownscaleFactor;

    WeightMapType weights;
    if (!LoadWeights("ML\\weights.bin", weights))
    {
        // implement
        throw std::exception("loadWeights");
    }

    uint64_t modelInputBufferSize = 0;
    uint64_t modelOutputBufferSize = 0;
    uint64_t intermediateBufferMaxSize[] = { 0, 0 };

    // Create an upscaled (nearest neighbor) version of the image first
    uint32_t modelInputSizes[] = { 1, 3, Height, Width };
    uint32_t upscaledInputSizes[4];
    CreateUpsampleLayer(modelInputSizes, &modelInputBufferSize, &modelOutputBufferSize, upscaledInputSizes, &m_dmlUpsampleOps[0]);

    uint32_t const filterSizes1[] = { 32, 3, 5, 5 };
    uint32_t intermediateInputSizes[2][4];
    CreateConvolutionLayer(modelInputSizes, filterSizes1, true, &modelInputBufferSize,
        &intermediateBufferMaxSize[0], intermediateInputSizes[0], &m_dmlConvOps[0]);
    CreateWeightTensors(commandList, weights, "conv1/weights", "conv1/BatchNorm/scale", "conv1/BatchNorm/shift",
        filterSizes1, &m_modelConvFilterWeights[0], &m_modelConvBiasWeights[0]);

    // Which intermediate resource to use as input for the current operation. The other will be
    // used as output. Then the next op will swap the order.
    int inputIndex = 0;

    uint32_t const filterSizes2[] = { 64, 32, 3, 3 };	// output filters
    CreateConvolutionLayer(intermediateInputSizes[inputIndex], filterSizes2, true, &intermediateBufferMaxSize[inputIndex],
        &intermediateBufferMaxSize[1 - inputIndex], intermediateInputSizes[1 - inputIndex], &m_dmlConvOps[1]);
    CreateWeightTensors(commandList, weights, "conv2/weights", "conv2/BatchNorm/scale", "conv2/BatchNorm/shift",
        filterSizes2, &m_modelConvFilterWeights[1], &m_modelConvBiasWeights[1]);
    inputIndex = 1 - inputIndex;

    uint32_t const filterSizes3[] = { 64, 64, 3, 3 };
    CreateConvolutionLayer(intermediateInputSizes[inputIndex], filterSizes3, true, &intermediateBufferMaxSize[inputIndex],
        &intermediateBufferMaxSize[1 - inputIndex], intermediateInputSizes[1 - inputIndex], &m_dmlConvOps[2]);
    CreateWeightTensors(commandList, weights, "conv3/weights", "conv3/BatchNorm/scale", "conv3/BatchNorm/shift",
        filterSizes3, &m_modelConvFilterWeights[2], &m_modelConvBiasWeights[2]);
    inputIndex = 1 - inputIndex;

    CreateUpsampleLayer(intermediateInputSizes[inputIndex], &intermediateBufferMaxSize[inputIndex],
        &intermediateBufferMaxSize[1 - inputIndex], intermediateInputSizes[1 - inputIndex], &m_dmlUpsampleOps[1]);
    inputIndex = 1 - inputIndex;

    uint32_t const filterSizes4[] = { 32, 64, 5, 5 };
    CreateConvolutionLayer(intermediateInputSizes[inputIndex], filterSizes4, true, &intermediateBufferMaxSize[inputIndex],
        &intermediateBufferMaxSize[1 - inputIndex], intermediateInputSizes[1 - inputIndex], &m_dmlConvOps[3]);
    CreateWeightTensors(commandList, weights, "conv_up1/conv/weights", "conv_up1/conv/BatchNorm/scale", "conv_up1/conv/BatchNorm/shift",
        filterSizes4, &m_modelConvFilterWeights[3], &m_modelConvBiasWeights[3]);
    inputIndex = 1 - inputIndex;

    uint32_t const filterSizes5[] = { 32, 32, 3, 3 };
    CreateConvolutionLayer(intermediateInputSizes[inputIndex], filterSizes5, true, &intermediateBufferMaxSize[inputIndex],
        &intermediateBufferMaxSize[1 - inputIndex], intermediateInputSizes[1 - inputIndex], &m_dmlConvOps[4]);
    CreateWeightTensors(commandList, weights, "conv4/weights", "conv4/BatchNorm/scale", "conv4/BatchNorm/shift",
        filterSizes5, &m_modelConvFilterWeights[4], &m_modelConvBiasWeights[4]);
    inputIndex = 1 - inputIndex;

    CreateConvolutionLayer(intermediateInputSizes[inputIndex], filterSizes5, true, &intermediateBufferMaxSize[inputIndex],
        &intermediateBufferMaxSize[1 - inputIndex], intermediateInputSizes[1 - inputIndex], &m_dmlConvOps[5]);
    CreateWeightTensors(commandList, weights, "conv5/weights", "conv5/BatchNorm/scale", "conv5/BatchNorm/shift",
        filterSizes5, &m_modelConvFilterWeights[5], &m_modelConvBiasWeights[5]);
    inputIndex = 1 - inputIndex;

    uint32_t const filterSizes6[] = { 3, 32, 3, 3 };
    CreateConvolutionLayer(intermediateInputSizes[inputIndex], filterSizes6, false, &intermediateBufferMaxSize[inputIndex],
        &intermediateBufferMaxSize[1 - inputIndex], intermediateInputSizes[1 - inputIndex], &m_dmlConvOps[6]);
    CreateWeightTensors(commandList, weights, "conv6/weights", nullptr, nullptr, filterSizes6,
        &m_modelConvFilterWeights[6], nullptr);
    inputIndex = 1 - inputIndex;

    // Resource for input tensor
    D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(modelInputBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto heapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    VERIFY_HRESULT(m_device.CreateCommittedResource(
        &heapDesc,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&m_modelInput)
    ));

    // Model result tensor is 2x larger in both dimensions
    resourceDesc.Width = modelOutputBufferSize;
    VERIFY_HRESULT(m_device.CreateCommittedResource(
        &heapDesc,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&m_modelOutput)
    ));

    // Finally add the residual to the original upsampled image
    assert(memcmp(upscaledInputSizes, intermediateInputSizes[inputIndex], 4 * sizeof(uint16_t)) == 0);
    CreateAdditionLayer(upscaledInputSizes, &m_dmlAddResidualOp);

    size_t upsampleOpDescriptorCount, convOpDescriptorCount, additionOpDescriptorCount;
    size_t upsampleDescriptorsIdx, convDescriptorsIdx, additionDescriptorsIdx;

    VERIFY_HRESULT(m_pDMLDevice->CreateOperatorInitializer(c_numUpsampleLayers, m_dmlUpsampleOps[0].GetAddressOf(), IID_PPV_ARGS(m_dmlOpInitializers[e_opUpsample].GetAddressOf())));
    upsampleOpDescriptorCount = GetDescriptorCount(c_numUpsampleLayers, m_dmlUpsampleOps[0].GetAddressOf(), m_dmlOpInitializers[e_opUpsample].Get());

    VERIFY_HRESULT(m_pDMLDevice->CreateOperatorInitializer(c_numConvLayers, m_dmlConvOps[0].GetAddressOf(), IID_PPV_ARGS(m_dmlOpInitializers[e_opConv].GetAddressOf())));
    convOpDescriptorCount = GetDescriptorCount(c_numConvLayers, m_dmlConvOps[0].GetAddressOf(), m_dmlOpInitializers[e_opConv].Get());

    VERIFY_HRESULT(m_pDMLDevice->CreateOperatorInitializer(1, m_dmlAddResidualOp.GetAddressOf(), IID_PPV_ARGS(m_dmlOpInitializers[e_opAdd].GetAddressOf())));
    additionOpDescriptorCount = GetDescriptorCount(1, m_dmlAddResidualOp.GetAddressOf(), m_dmlOpInitializers[e_opAdd].Get());

    upsampleDescriptorsIdx = 0;
    convDescriptorsIdx = upsampleDescriptorsIdx + upsampleOpDescriptorCount * c_numUpsampleLayers;
    additionDescriptorsIdx = convDescriptorsIdx + convOpDescriptorCount * c_numConvLayers;

    UINT modelDescriptorCount = 2; // Model input and output
    UINT requestedDescriptorCount = additionDescriptorsIdx + additionOpDescriptorCount + modelDescriptorCount;

    UINT modelInputDescriptorIndex = additionDescriptorsIdx + additionOpDescriptorCount;
    UINT modelOutputDescriptorIndex = modelInputDescriptorIndex + 1;

    // Describe and create a UAV for the original input tensor.
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R16_FLOAT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = static_cast<UINT>(modelInputBufferSize / sizeof(uint16_t));
    uavDesc.Buffer.StructureByteStride = 0;
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    m_device.CreateUnorderedAccessView(m_modelInput.Get(), nullptr, &uavDesc, CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapCPUBase, modelInputDescriptorIndex, descriptorSize));
    m_modelInputUAV = CD3DX12_GPU_DESCRIPTOR_HANDLE(descriptorHeapGPUBase, modelInputDescriptorIndex, descriptorSize);

    // Describe and create a SRV for the final result tensor.
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R16_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = static_cast<UINT>(modelOutputBufferSize / sizeof(uint16_t));
    srvDesc.Buffer.StructureByteStride = 0;
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    m_device.CreateShaderResourceView(m_modelOutput.Get(), &srvDesc, CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapCPUBase, modelOutputDescriptorIndex, descriptorSize));
    m_modelOutputSRV = CD3DX12_GPU_DESCRIPTOR_HANDLE(descriptorHeapGPUBase, modelOutputDescriptorIndex, descriptorSize);

    // Create two resources for intermediate layer results. Each layer will ping-pong between these. They're each large
    // enough to hold the largest intermediate result required.
    for (int i = 0; i < 2; i++)
    {
        resourceDesc.Width = intermediateBufferMaxSize[i];
        VERIFY_HRESULT(m_device.CreateCommittedResource(
            &heapDesc,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&m_modelIntermediateResult[i])
        ));
    }

    // Make sure the requested amount is lower than the hard-coded max amount
    VERIFY(requestedDescriptorCount < m_RequiredDescriptors);

    // Create any persistent resources required for the operators.
    {
        for (int i = 0; i < c_numUpsampleLayers + c_numConvLayers + 1; i++)
        {
            IDMLCompiledOperator* currentOp;
            ID3D12Resource** persistentResource;
            if (i < c_numUpsampleLayers)
            {
                currentOp = m_dmlUpsampleOps[i].Get();
                persistentResource = m_modelUpsamplePersistentResources[i].ReleaseAndGetAddressOf();
            }
            else if (i < c_numUpsampleLayers + c_numConvLayers)
            {
                currentOp = m_dmlConvOps[i - c_numUpsampleLayers].Get();
                persistentResource = m_modelConvPersistentResources[i - c_numUpsampleLayers].ReleaseAndGetAddressOf();
            }
            else
            {
                currentOp = m_dmlAddResidualOp.Get();
                persistentResource = m_modelAddPersistentResource.ReleaseAndGetAddressOf();
            }

            auto bindingProps = currentOp->GetBindingProperties();

            if (bindingProps.PersistentResourceSize > 0)
            {
                D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bindingProps.PersistentResourceSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
                VERIFY_HRESULT(m_device.CreateCommittedResource(
                    &heapDesc,
                    D3D12_HEAP_FLAG_NONE,
                    &resourceDesc,
                    D3D12_RESOURCE_STATE_COMMON,
                    nullptr,
                    IID_PPV_ARGS(persistentResource)));
            }
        }
    }

    Microsoft::WRL::ComPtr<IDMLBindingTable> initBindingTable;
    const DML_BINDING_DESC emptyBindingDesc = { DML_BINDING_TYPE_NONE, nullptr };
    // Upsample layers
    {
        // Bind resources for initialization.
        auto bindingProps = m_dmlOpInitializers[e_opUpsample]->GetBindingProperties();
        // The DML API guarantees that initialization never uses a persistent resource.
        assert(bindingProps.PersistentResourceSize == 0);

        DML_BINDING_TABLE_DESC tableDesc = {
            m_dmlOpInitializers[e_opUpsample].Get(),
            CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapCPUBase, upsampleDescriptorsIdx, descriptorSize),
            CD3DX12_GPU_DESCRIPTOR_HANDLE(descriptorHeapGPUBase, upsampleDescriptorsIdx, descriptorSize),
            bindingProps.RequiredDescriptorCount
        };
        VERIFY_HRESULT(m_pDMLDevice->CreateBindingTable(&tableDesc, IID_PPV_ARGS(&initBindingTable)));

        // If the operator requires a persistent resource, it must be bound as output for the initializer.
        DML_BUFFER_BINDING upsamplePersistentBuffers[c_numUpsampleLayers];
        DML_BINDING_DESC upsamplePersistentBindings[c_numUpsampleLayers];
        for (int i = 0; i < c_numUpsampleLayers; i++)
        {
            if (m_modelUpsamplePersistentResources[i].Get() != nullptr)
            {
                upsamplePersistentBuffers[i] = { m_modelUpsamplePersistentResources[i].Get(), 0, m_modelUpsamplePersistentResources[i]->GetDesc().Width };
                upsamplePersistentBindings[i] = { DML_BINDING_TYPE_BUFFER, &upsamplePersistentBuffers[i] };
            }
            else
                upsamplePersistentBindings[i] = emptyBindingDesc;
        }

        // The inputs will vary each frame, so don't bind inputs at initialization.
        initBindingTable->BindInputs(0, nullptr);
        initBindingTable->BindOutputs(c_numUpsampleLayers, upsamplePersistentBindings);
        BindTempResourceIfNeeded(m_device, bindingProps, initBindingTable.Get(), m_modelInitTemporaryResources[e_opUpsample].ReleaseAndGetAddressOf());

        // Run initialization
        m_pCommandRecorder->RecordDispatch(&commandList, m_dmlOpInitializers[e_opUpsample].Get(), initBindingTable.Get());

        // Bind resources for execution
        for (int i = 0; i < c_numUpsampleLayers; i++)
        {
            bindingProps = m_dmlUpsampleOps[i]->GetBindingProperties();

            tableDesc = {
                m_dmlUpsampleOps[i].Get(),
                CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapCPUBase, upsampleDescriptorsIdx + i * upsampleOpDescriptorCount, descriptorSize),
                CD3DX12_GPU_DESCRIPTOR_HANDLE(descriptorHeapGPUBase, upsampleDescriptorsIdx + i * upsampleOpDescriptorCount, descriptorSize),
                bindingProps.RequiredDescriptorCount
            };
            VERIFY_HRESULT(m_pDMLDevice->CreateBindingTable(&tableDesc, IID_PPV_ARGS(m_dmlUpsampleBindings[i].ReleaseAndGetAddressOf())));

            auto inputResource = (i == 0) ? m_modelInput : m_modelIntermediateResult[0];
            auto outputResource = (i == 0) ? m_modelOutput : m_modelIntermediateResult[1];

            DML_BUFFER_BINDING inputBufferBinding = { inputResource.Get(), 0, inputResource->GetDesc().Width };
            DML_BINDING_DESC inputBinding = { DML_BINDING_TYPE_BUFFER, &inputBufferBinding };
            DML_BUFFER_BINDING outputBufferBinding = { outputResource.Get(), 0, outputResource->GetDesc().Width };
            DML_BINDING_DESC outputBinding = { DML_BINDING_TYPE_BUFFER, &outputBufferBinding };

            m_dmlUpsampleBindings[i]->BindInputs(1, &inputBinding);
            m_dmlUpsampleBindings[i]->BindOutputs(1, &outputBinding);
            BindTempResourceIfNeeded(m_device, bindingProps, m_dmlUpsampleBindings[i].Get(), m_modelUpsampleTemporaryResources[i].ReleaseAndGetAddressOf());

            if (m_modelUpsamplePersistentResources[i].Get() != nullptr)
                m_dmlUpsampleBindings[i]->BindPersistentResource(&upsamplePersistentBindings[i]);
        }
    }

    // Convolution layers
    {
        // Bind resources for initialization
        auto bindingProps = m_dmlOpInitializers[e_opConv]->GetBindingProperties();
        assert(bindingProps.PersistentResourceSize == 0);

        DML_BINDING_TABLE_DESC tableDesc = {
            m_dmlOpInitializers[e_opConv].Get(),
            CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapCPUBase, convDescriptorsIdx, descriptorSize),
            CD3DX12_GPU_DESCRIPTOR_HANDLE(descriptorHeapGPUBase, convDescriptorsIdx, descriptorSize),
            bindingProps.RequiredDescriptorCount
        };
        VERIFY_HRESULT(initBindingTable->Reset(&tableDesc));


        const DML_BUFFER_BINDING emptyBufferBinding = { nullptr, 0, 0 };
#if DML_MANAGED_WEIGHTS
        // Bind the weight tensors at initialization instead of at execution. This lets DirectML reformat them
        // and improve performance on some hardware.
        DML_BUFFER_BINDING convBufferBindings[][3] = {
            { emptyBufferBinding, { m_modelConvFilterWeights[0].Get(), 0, m_modelConvFilterWeights[0]->GetDesc().Width }, { m_modelConvBiasWeights[0].Get(), 0, m_modelConvBiasWeights[0]->GetDesc().Width } },
            { emptyBufferBinding, { m_modelConvFilterWeights[1].Get(), 0, m_modelConvFilterWeights[1]->GetDesc().Width }, { m_modelConvBiasWeights[1].Get(), 0, m_modelConvBiasWeights[1]->GetDesc().Width } },
            { emptyBufferBinding, { m_modelConvFilterWeights[2].Get(), 0, m_modelConvFilterWeights[2]->GetDesc().Width }, { m_modelConvBiasWeights[2].Get(), 0, m_modelConvBiasWeights[2]->GetDesc().Width } },
            { emptyBufferBinding, { m_modelConvFilterWeights[3].Get(), 0, m_modelConvFilterWeights[3]->GetDesc().Width }, { m_modelConvBiasWeights[3].Get(), 0, m_modelConvBiasWeights[3]->GetDesc().Width } },
            { emptyBufferBinding, { m_modelConvFilterWeights[4].Get(), 0, m_modelConvFilterWeights[4]->GetDesc().Width }, { m_modelConvBiasWeights[4].Get(), 0, m_modelConvBiasWeights[4]->GetDesc().Width } },
            { emptyBufferBinding, { m_modelConvFilterWeights[5].Get(), 0, m_modelConvFilterWeights[5]->GetDesc().Width }, { m_modelConvBiasWeights[5].Get(), 0, m_modelConvBiasWeights[5]->GetDesc().Width } },
            { emptyBufferBinding, { m_modelConvFilterWeights[6].Get(), 0, m_modelConvFilterWeights[6]->GetDesc().Width }, emptyBufferBinding }	// last layer has no bias
        };

        DML_BUFFER_ARRAY_BINDING convBufferArrayBindings[] = {
            { 3, convBufferBindings[0] },
            { 3, convBufferBindings[1] },
            { 3, convBufferBindings[2] },
            { 3, convBufferBindings[3] },
            { 3, convBufferBindings[4] },
            { 3, convBufferBindings[5] },
            { 3, convBufferBindings[6] },
        };

        DML_BINDING_DESC convInBindings[] = {
            { DML_BINDING_TYPE_BUFFER_ARRAY, &convBufferArrayBindings[0] },
            { DML_BINDING_TYPE_BUFFER_ARRAY, &convBufferArrayBindings[1] },
            { DML_BINDING_TYPE_BUFFER_ARRAY, &convBufferArrayBindings[2] },
            { DML_BINDING_TYPE_BUFFER_ARRAY, &convBufferArrayBindings[3] },
            { DML_BINDING_TYPE_BUFFER_ARRAY, &convBufferArrayBindings[4] },
            { DML_BINDING_TYPE_BUFFER_ARRAY, &convBufferArrayBindings[5] },
            { DML_BINDING_TYPE_BUFFER_ARRAY, &convBufferArrayBindings[6] }
        };

        initBindingTable->BindInputs(c_numConvLayers, convInBindings);
#else
        initBindingTable->BindInputs(0, nullptr);
#endif

        // If the operator requires a persistent resource, it must be bound as output for the initializer.
        DML_BUFFER_BINDING convPersistentBuffers[c_numConvLayers];
        DML_BINDING_DESC convPersistentBindings[c_numConvLayers];
        for (int i = 0; i < c_numConvLayers; i++)
        {
            if (m_modelConvPersistentResources[i].Get() != nullptr)
            {
                convPersistentBuffers[i] = { m_modelConvPersistentResources[i].Get(), 0, m_modelConvPersistentResources[i]->GetDesc().Width };
                convPersistentBindings[i] = { DML_BINDING_TYPE_BUFFER, &convPersistentBuffers[i] };
            }
            else
                convPersistentBindings[i] = emptyBindingDesc;
        }

        initBindingTable->BindOutputs(c_numConvLayers, convPersistentBindings);
        BindTempResourceIfNeeded(m_device, bindingProps, initBindingTable.Get(), m_modelInitTemporaryResources[e_opConv].ReleaseAndGetAddressOf());

        // Run initialization
        m_pCommandRecorder->RecordDispatch(&commandList, m_dmlOpInitializers[e_opConv].Get(), initBindingTable.Get());

        // Bind resources for execution
        for (int i = 0; i < c_numConvLayers; i++)
        {
            bindingProps = m_dmlConvOps[i]->GetBindingProperties();

            tableDesc = {
                m_dmlConvOps[i].Get(),
                CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapCPUBase, convDescriptorsIdx + i * convOpDescriptorCount, descriptorSize),
                CD3DX12_GPU_DESCRIPTOR_HANDLE(descriptorHeapGPUBase, convDescriptorsIdx + i * convOpDescriptorCount, descriptorSize),
                bindingProps.RequiredDescriptorCount
            };
            VERIFY_HRESULT(m_pDMLDevice->CreateBindingTable(&tableDesc, IID_PPV_ARGS(m_dmlConvBindings[i].ReleaseAndGetAddressOf())));

            // See table at the beginning of the function for the mapping of ops to resources.
            auto inputResource = (i == 0) ? m_modelInput : ((i == 1 || i == 4 || i == 6) ? m_modelIntermediateResult[0] : m_modelIntermediateResult[1]);
            auto outputResource = (i == 1 || i == 4 || i == 6) ? m_modelIntermediateResult[1] : m_modelIntermediateResult[0];

            DML_BUFFER_BINDING inputBufferBinding = { inputResource.Get(), 0, inputResource->GetDesc().Width };
            DML_BINDING_DESC inputBinding = { DML_BINDING_TYPE_BUFFER, &inputBufferBinding };

            DML_BUFFER_BINDING outputBufferBinding = { outputResource.Get(), 0, outputResource->GetDesc().Width };
            DML_BINDING_DESC outputBinding = { DML_BINDING_TYPE_BUFFER, &outputBufferBinding };

#if DML_MANAGED_WEIGHTS
            // The weights are stored in the persistent resource and shouldn't be bound separately.
            DML_BINDING_DESC inputBindings[] = { inputBinding, emptyBindingDesc, emptyBindingDesc };
#else
            // Bind the weight resources
            DML_BUFFER_BINDING filterBufferBinding = { m_modelConvFilterWeights[i].Get(), 0, m_modelConvFilterWeights[i]->GetDesc().Width };
            DML_BINDING_DESC filterBinding = { DML_BINDING_TYPE_BUFFER, &filterBufferBinding };

            DML_BUFFER_BINDING biasBufferBinding;
            DML_BINDING_DESC biasBinding;
            if (i == 6)
            {
                biasBinding = emptyBindingDesc;	// last layer has no bias
            }
            else
            {
                biasBufferBinding = { m_modelConvBiasWeights[i].Get(), 0, m_modelConvBiasWeights[i]->GetDesc().Width };
                biasBinding = { DML_BINDING_TYPE_BUFFER, &biasBufferBinding };
            }

            DML_BINDING_DESC inputBindings[] = { inputBinding, filterBinding, biasBinding };
#endif
            m_dmlConvBindings[i]->BindInputs(3, inputBindings);
            m_dmlConvBindings[i]->BindOutputs(1, &outputBinding);
            BindTempResourceIfNeeded(m_device, bindingProps, m_dmlConvBindings[i].Get(), m_modelConvTemporaryResources[i].ReleaseAndGetAddressOf());

            if (m_modelConvPersistentResources[i].Get() != nullptr)
                m_dmlConvBindings[i]->BindPersistentResource(&convPersistentBindings[i]);
        }
    }

    // Addition layer
    {
        // Bind resources for initialization.
        auto bindingProps = m_dmlOpInitializers[e_opAdd]->GetBindingProperties();
        assert(bindingProps.PersistentResourceSize == 0);

        DML_BINDING_TABLE_DESC tableDesc = {
            m_dmlOpInitializers[e_opAdd].Get(),
            CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapCPUBase, additionDescriptorsIdx, descriptorSize),
            CD3DX12_GPU_DESCRIPTOR_HANDLE(descriptorHeapGPUBase, additionDescriptorsIdx, descriptorSize),
            bindingProps.RequiredDescriptorCount
        };
        VERIFY_HRESULT(initBindingTable->Reset(&tableDesc));

        // If the operator requires a persistent resource, it must be bound as output for the initializer.
        DML_BUFFER_BINDING addPersistentBuffer;
        DML_BINDING_DESC addPersistentBinding;
        if (m_modelAddPersistentResource.Get() != nullptr)
        {
            addPersistentBuffer = { m_modelAddPersistentResource.Get(), 0, m_modelAddPersistentResource->GetDesc().Width };
            addPersistentBinding = { DML_BINDING_TYPE_BUFFER, &addPersistentBuffer };
        }
        else
            addPersistentBinding = emptyBindingDesc;

        initBindingTable->BindInputs(0, nullptr);
        initBindingTable->BindOutputs(1, &addPersistentBinding);
        BindTempResourceIfNeeded(m_device, bindingProps, initBindingTable.Get(), m_modelInitTemporaryResources[e_opAdd].ReleaseAndGetAddressOf());

        // Run initialization
        m_pCommandRecorder->RecordDispatch(&commandList, m_dmlOpInitializers[e_opAdd].Get(), initBindingTable.Get());

        // Bind resources for execution
        {
            bindingProps = m_dmlAddResidualOp->GetBindingProperties();

            tableDesc = {
                m_dmlAddResidualOp.Get(),
                CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapCPUBase, additionDescriptorsIdx, descriptorSize),
                CD3DX12_GPU_DESCRIPTOR_HANDLE(descriptorHeapGPUBase, additionDescriptorsIdx, descriptorSize),
                bindingProps.RequiredDescriptorCount
            };
            VERIFY_HRESULT(m_pDMLDevice->CreateBindingTable(&tableDesc, IID_PPV_ARGS(m_dmlAddResidualBinding.ReleaseAndGetAddressOf())));

            // m_modelOutput will already hold the result of the first upsample operation. We add the result of
            // the last convolution (the residual) to it in-place to get the final result.
            DML_BUFFER_BINDING input0BufferBinding = { m_modelIntermediateResult[1].Get(), 0, m_modelIntermediateResult[1]->GetDesc().Width };
            DML_BINDING_DESC input0Binding = { DML_BINDING_TYPE_BUFFER, &input0BufferBinding };
            DML_BUFFER_BINDING input1BufferBinding = { m_modelOutput.Get(), 0, m_modelOutput->GetDesc().Width };
            DML_BINDING_DESC input1Binding = { DML_BINDING_TYPE_BUFFER, &input1BufferBinding };
            DML_BUFFER_BINDING outputBufferBinding = { m_modelOutput.Get(), 0, m_modelOutput->GetDesc().Width };
            DML_BINDING_DESC outputBinding = { DML_BINDING_TYPE_BUFFER, &outputBufferBinding };

            DML_BINDING_DESC inputBindings[] = { input0Binding, input1Binding };
            m_dmlAddResidualBinding->BindInputs(2, inputBindings);
            m_dmlAddResidualBinding->BindOutputs(1, &outputBinding);
            BindTempResourceIfNeeded(m_device, bindingProps, m_dmlAddResidualBinding.Get(), m_modelAddTemporaryResource.ReleaseAndGetAddressOf());

            if (m_modelAddPersistentResource.Get() != nullptr)
                m_dmlAddResidualBinding->BindPersistentResource(&addPersistentBinding);
        }
    }
}

void GetStrides(
    _In_reads_(4) const uint32_t* sizes,
    DirectMLSuperResolutionPass::TensorLayout layout,
    _Out_writes_(4) uint32_t* stridesOut
)
{
    switch (layout)
    {
    case DirectMLSuperResolutionPass::TensorLayout::NHWC:
        stridesOut[0] = sizes[1] * sizes[2] * sizes[3];
        stridesOut[1] = 1;
        stridesOut[2] = sizes[1] * sizes[3];
        stridesOut[3] = sizes[1];
        break;

    default:
        stridesOut[0] = sizes[1] * sizes[2] * sizes[3];
        stridesOut[1] = sizes[2] * sizes[3];
        stridesOut[2] = sizes[3];
        stridesOut[3] = 1;
    }
}

void DirectMLSuperResolutionPass::CreateConvolutionLayer(
    _In_reads_(4) const uint32_t* inputSizes,
    _In_reads_(4) const uint32_t* filterSizes,
    bool useBiasAndActivation,
    _Inout_updates_(1) uint64_t* inputBufferRequiredSize,
    _Inout_updates_(1) uint64_t* outputBufferRequiredSize,
    _Out_writes_(4) uint32_t* outputSizesOut,
    _Out_writes_(1) IDMLCompiledOperator** compiledOpOut)
{
    // Describe input and output tensors    
    uint32_t inputStrides[4];
    GetStrides(inputSizes, m_tensorLayout, inputStrides);

    uint64_t inputBufferSize = DMLCalcBufferTensorSize(DML_TENSOR_DATA_TYPE_FLOAT16, 4, inputSizes, inputStrides);
    *inputBufferRequiredSize = std::max(inputBufferSize, *inputBufferRequiredSize);

    DML_BUFFER_TENSOR_DESC inputBufferDesc = { DML_TENSOR_DATA_TYPE_FLOAT16, DML_TENSOR_FLAG_NONE, 4, inputSizes, inputStrides, inputBufferSize, 0 };
    DML_TENSOR_DESC inputDesc = { DML_TENSOR_TYPE_BUFFER, &inputBufferDesc };

    // The output shape has as many channels as there are convolution filters.
    outputSizesOut[0] = inputSizes[0];
    outputSizesOut[1] = filterSizes[0];
    outputSizesOut[2] = inputSizes[2];
    outputSizesOut[3] = inputSizes[3];

    uint32_t outputStrides[4];
    GetStrides(outputSizesOut, m_tensorLayout, outputStrides);

    uint64_t outputBufferSize = DMLCalcBufferTensorSize(DML_TENSOR_DATA_TYPE_FLOAT16, 4, outputSizesOut, outputStrides);
    *outputBufferRequiredSize = std::max(outputBufferSize, *outputBufferRequiredSize);

    DML_BUFFER_TENSOR_DESC outputBufferDesc = { DML_TENSOR_DATA_TYPE_FLOAT16, DML_TENSOR_FLAG_NONE, 4, outputSizesOut, outputStrides, outputBufferSize, 0 };
    DML_TENSOR_DESC outputDesc = { DML_TENSOR_TYPE_BUFFER, &outputBufferDesc };

    // Describe weight tensors
    uint32_t filterStrides[4];
    GetStrides(filterSizes, m_tensorLayout, filterStrides);
    uint64_t filterBufferSize = DMLCalcBufferTensorSize(DML_TENSOR_DATA_TYPE_FLOAT16, 4, filterSizes, filterStrides);

#if DML_MANAGED_WEIGHTS
    DML_BUFFER_TENSOR_DESC filterBufferDesc = { DML_TENSOR_DATA_TYPE_FLOAT16, DML_TENSOR_FLAG_OWNED_BY_DML, 4, filterSizes, filterStrides, filterBufferSize, 0 };
#else
    DML_BUFFER_TENSOR_DESC filterBufferDesc = { DML_TENSOR_DATA_TYPE_FLOAT16, DML_TENSOR_FLAG_NONE, 4, filterSizes, filterStrides, filterBufferSize, 0 };
#endif
    DML_TENSOR_DESC filterDesc = { DML_TENSOR_TYPE_BUFFER, &filterBufferDesc };

    uint32_t biasSizes[] = { 1, filterSizes[0], 1, 1 };	// One bias per output channel    
    uint32_t biasStrides[4];
    GetStrides(biasSizes, m_tensorLayout, biasStrides);
    uint64_t biasBufferSize = DMLCalcBufferTensorSize(DML_TENSOR_DATA_TYPE_FLOAT16, 4, biasSizes, biasStrides);

#if DML_MANAGED_WEIGHTS
    DML_BUFFER_TENSOR_DESC biasBufferDesc = { DML_TENSOR_DATA_TYPE_FLOAT16, DML_TENSOR_FLAG_OWNED_BY_DML, 4, biasSizes, biasStrides, biasBufferSize, 0 };
#else
    DML_BUFFER_TENSOR_DESC biasBufferDesc = { DML_TENSOR_DATA_TYPE_FLOAT16, DML_TENSOR_FLAG_NONE, 4, biasSizes, biasStrides, biasBufferSize, 0 };
#endif
    DML_TENSOR_DESC biasDesc = { DML_TENSOR_TYPE_BUFFER, &biasBufferDesc };

    // Describe, create, and compile convolution operator

    // The output size of a convolution operation is given by:
    //  height = (inputHeight - filterHeight + 2*paddingHeight) / filterStride + 1
    //  width  = (inputWidth  - filterWidth  + 2*paddingWidth ) / filterStride + 1
    //
    // We want to preserve the height and width, so assuming stride is 1, we get:
    //  paddingHeight = (filterHeight - 1) / 2
    //  paddingWidth  = (filterWidth  - 1) / 2
    // If padding is fractional, we pad unevenly with ceil/floor.
    UINT paddingHeightTop = static_cast<UINT>(ceil((filterSizes[2] - 1) / 2.0f));
    UINT paddingHeightBottom = static_cast<UINT>(floor((filterSizes[2] - 1) / 2.0f));
    UINT paddingWidthLeft = static_cast<UINT>(ceil((filterSizes[3] - 1) / 2.0f));
    UINT paddingWidthRight = static_cast<UINT>(floor((filterSizes[3] - 1) / 2.0f));

    UINT strides[] = { 1, 1 };
    UINT dilations[] = { 1, 1 };
    UINT startPadding[] = { paddingHeightTop, paddingWidthLeft };
    UINT endPadding[] = { paddingHeightBottom, paddingWidthRight };
    UINT outputPadding[] = { 0, 0 };

    DML_ACTIVATION_RELU_OPERATOR_DESC fusedReluDesc = { 0 };
    DML_OPERATOR_DESC activationDesc = { DML_OPERATOR_ACTIVATION_RELU, &fusedReluDesc };

    DML_CONVOLUTION_OPERATOR_DESC convDesc = {
        &inputDesc,
        &filterDesc,
        useBiasAndActivation ? &biasDesc : nullptr,
        &outputDesc,
        DML_CONVOLUTION_MODE_CROSS_CORRELATION,
        DML_CONVOLUTION_DIRECTION_FORWARD,
        2,
        strides,
        dilations,
        startPadding,
        endPadding,
        outputPadding,
        1,
        useBiasAndActivation ? &activationDesc : nullptr
    };
    DML_OPERATOR_DESC opDesc = { DML_OPERATOR_CONVOLUTION, &convDesc };

    ComPtr<IDMLOperator> op;
    VERIFY_HRESULT(m_pDMLDevice->CreateOperator(&opDesc, IID_PPV_ARGS(op.ReleaseAndGetAddressOf())));
    VERIFY_HRESULT(m_pDMLDevice->CompileOperator(op.Get(), DML_EXECUTION_FLAG_ALLOW_HALF_PRECISION_COMPUTATION, IID_PPV_ARGS(compiledOpOut)));
}

void DirectMLSuperResolutionPass::CreateUpsampleLayer(
    _In_reads_(4) const uint32_t* inputSizes,
    _Inout_updates_(1) uint64_t* inputBufferRequiredSize,
    _Inout_updates_(1) uint64_t* outputBufferRequiredSize,
    _Out_writes_(4) uint32_t* outputSizesOut,
    _Out_writes_(1) IDMLCompiledOperator** compiledOpOut)
{
    // Describe input and output tensors
    uint32_t inputStrides[4];
    GetStrides(inputSizes, m_tensorLayout, inputStrides);

    uint64_t inputBufferSize = DMLCalcBufferTensorSize(DML_TENSOR_DATA_TYPE_FLOAT16, 4, inputSizes, inputStrides);
    // Because we can resuse resources for tensor storage, this tracks the resource size needed to hold the
    // largest possible tensor requested.
    *inputBufferRequiredSize = std::max(inputBufferSize, *inputBufferRequiredSize);

    DML_BUFFER_TENSOR_DESC inputBufferDesc = { DML_TENSOR_DATA_TYPE_FLOAT16, DML_TENSOR_FLAG_NONE, 4, inputSizes, inputStrides, inputBufferSize, 0 };
    DML_TENSOR_DESC inputDesc = { DML_TENSOR_TYPE_BUFFER, &inputBufferDesc };

    // Output size is double in height and width
    outputSizesOut[0] = inputSizes[0];
    outputSizesOut[1] = inputSizes[1];
    outputSizesOut[2] = inputSizes[2] * 2;
    outputSizesOut[3] = inputSizes[3] * 2;

    uint32_t outputStrides[4];
    GetStrides(outputSizesOut, m_tensorLayout, outputStrides);

    uint64_t outputBufferSize = DMLCalcBufferTensorSize(DML_TENSOR_DATA_TYPE_FLOAT16, 4, outputSizesOut, outputStrides);
    *outputBufferRequiredSize = std::max(outputBufferSize, *outputBufferRequiredSize);

    DML_BUFFER_TENSOR_DESC outputBufferDesc = { DML_TENSOR_DATA_TYPE_FLOAT16, DML_TENSOR_FLAG_NONE, 4, outputSizesOut, outputStrides, outputBufferSize, 0 };
    DML_TENSOR_DESC outputDesc = { DML_TENSOR_TYPE_BUFFER, &outputBufferDesc };

    // Describe, create, and compile upsample operator
    DML_UPSAMPLE_2D_OPERATOR_DESC upsampleDesc = { &inputDesc, &outputDesc, {2, 2}, DML_INTERPOLATION_MODE_NEAREST_NEIGHBOR };
    DML_OPERATOR_DESC opDesc = { DML_OPERATOR_UPSAMPLE_2D, &upsampleDesc };

    ComPtr<IDMLOperator> op;
    VERIFY_HRESULT(m_pDMLDevice->CreateOperator(&opDesc, IID_PPV_ARGS(op.ReleaseAndGetAddressOf())));
    VERIFY_HRESULT(m_pDMLDevice->CompileOperator(op.Get(), DML_EXECUTION_FLAG_ALLOW_HALF_PRECISION_COMPUTATION, IID_PPV_ARGS(compiledOpOut)));
}


void DirectMLSuperResolutionPass::CreateAdditionLayer(
    _In_reads_(4) const uint32_t* inputSizes,
    _Out_writes_(1) IDMLCompiledOperator** compiledOpOut)
{
    // Describe input and output tensors
    uint32_t strides[4];
    GetStrides(inputSizes, m_tensorLayout, strides);
    uint64_t bufferSize = DMLCalcBufferTensorSize(DML_TENSOR_DATA_TYPE_FLOAT16, 4, inputSizes, strides);

    DML_BUFFER_TENSOR_DESC bufferDesc = { DML_TENSOR_DATA_TYPE_FLOAT16, DML_TENSOR_FLAG_NONE, 4, inputSizes, strides, bufferSize, 0 };
    DML_TENSOR_DESC tensorDesc = { DML_TENSOR_TYPE_BUFFER, &bufferDesc };

    // Describe, create, and compile elementwise addition operator
    // Inputs and output are all the same size and use the same tensor desc.
    DML_ELEMENT_WISE_ADD_OPERATOR_DESC addDesc = { &tensorDesc, &tensorDesc, &tensorDesc };
    DML_OPERATOR_DESC opDesc = { DML_OPERATOR_ELEMENT_WISE_ADD, &addDesc };

    ComPtr<IDMLOperator> op;
    VERIFY_HRESULT(m_pDMLDevice->CreateOperator(&opDesc, IID_PPV_ARGS(op.ReleaseAndGetAddressOf())));
    VERIFY_HRESULT(m_pDMLDevice->CompileOperator(op.Get(), DML_EXECUTION_FLAG_ALLOW_HALF_PRECISION_COMPUTATION, IID_PPV_ARGS(compiledOpOut)));
}


D3D12_GPU_DESCRIPTOR_HANDLE DirectMLSuperResolutionPass::Run(
	ID3D12GraphicsCommandList& commandList,
	PassResource OutputBuffer,
	D3D12_GPU_DESCRIPTOR_HANDLE InputTexture,
	UINT inputWidth,
	UINT inputHeight)
{
	PIXScopedEvent(&commandList, PIX_COLOR_DEFAULT, L"DirectML Super Resolution");

	auto uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);


    // Convert image to tensor format (original texture -> model input)
    {
        PIXScopedEvent(&commandList, PIX_COLOR_DEFAULT, L"Convert input image");
        commandList.SetPipelineState(m_pImageToTensorPSO.Get());
        commandList.SetComputeRootSignature(m_pRootSignature.Get());


        DirectMLConstants constants = {};
        constants.Resolution = { inputWidth, inputHeight };
        constants.UseNHWC = (m_tensorLayout == TensorLayout::NHWC);
        commandList.SetComputeRoot32BitConstants(DirectMLSuperResolutionRootSignatureParameters::ConstantsParam, sizeof(constants) / sizeof(UINT32), &constants, 0);

        commandList.SetComputeRootDescriptorTable(DirectMLSuperResolutionRootSignatureParameters::Input, InputTexture);
        commandList.SetComputeRootDescriptorTable(DirectMLSuperResolutionRootSignatureParameters::Output, m_modelInputUAV);

        UINT DispatchWidth = (inputWidth - 1) / DIRECTML_THREAD_GROUP_WIDTH + 1;
        UINT DispatchHeight = (inputHeight - 1) / DIRECTML_THREAD_GROUP_HEIGHT + 1;

        commandList.Dispatch(DispatchWidth, DispatchHeight, 1);
        commandList.ResourceBarrier(1, &uavBarrier);
    }

	// Create an upsampled (nearest neighbor) version of the image first
	m_pCommandRecorder->RecordDispatch(&commandList, m_dmlUpsampleOps[0].Get(), m_dmlUpsampleBindings[0].Get());
	// No UAV barrier is required here since we don't use the result right away.

	// Run the intermediate model steps: 3 convolutions (with premultiplied batch normalization
	// baked into the weights), an upsample, 3 convolutions w/ premultiplied batch norm, 1 final convolution.
	// This generates a residual image.
	for (int i = 0; i < c_numConvLayers; i++)
	{
		// Convolution
		m_pCommandRecorder->RecordDispatch(&commandList, m_dmlConvOps[i].Get(), m_dmlConvBindings[i].Get());
		commandList.ResourceBarrier(1, &uavBarrier);

		if (i == 2)
		{
			// Intermediate upsample
			m_pCommandRecorder->RecordDispatch(&commandList, m_dmlUpsampleOps[1].Get(), m_dmlUpsampleBindings[1].Get());
			commandList.ResourceBarrier(1, &uavBarrier);
		}
	}

	// Add the residual image to the original nearest-neighbor upscale
	m_pCommandRecorder->RecordDispatch(&commandList, m_dmlAddResidualOp.Get(), m_dmlAddResidualBinding.Get());
    commandList.ResourceBarrier(1, &uavBarrier);

    // Render either the DML result or a bilinear upscale to a texture
    {
        PIXScopedEvent(&commandList, PIX_COLOR_DEFAULT, L"Render to texture");

        commandList.SetPipelineState(m_pTensorToImagePSO.Get());
        commandList.SetComputeRootSignature(m_pRootSignature.Get());

        UINT outputWidth = inputWidth * 2;
        UINT outputHeight = inputHeight * 2;

        DirectMLConstants constants = {};
        constants.Resolution = { outputWidth, outputHeight };
        constants.UseNHWC = (m_tensorLayout == TensorLayout::NHWC);
        commandList.SetComputeRoot32BitConstants(DirectMLSuperResolutionRootSignatureParameters::ConstantsParam, sizeof(constants) / sizeof(UINT32), &constants, 0);

        commandList.SetComputeRootDescriptorTable(DirectMLSuperResolutionRootSignatureParameters::Input, m_modelOutputSRV);
        commandList.SetComputeRootDescriptorTable(DirectMLSuperResolutionRootSignatureParameters::Output, OutputBuffer.m_uavHandle);

        UINT DispatchWidth = (outputWidth - 1) / DIRECTML_THREAD_GROUP_WIDTH + 1;
        UINT DispatchHeight = (outputHeight - 1) / DIRECTML_THREAD_GROUP_HEIGHT + 1;

        commandList.Dispatch(DispatchWidth, DispatchHeight, 1);
        commandList.ResourceBarrier(1, &uavBarrier);
    }

	return OutputBuffer.m_srvHandle;
}

//------------------------------------------------------------------------------------------------
// Heap-allocating UpdateSubresources implementation
inline UINT64 UpdateSubresourcesHelper(
    ID3D12Device* pDevice,
    _In_ ID3D12GraphicsCommandList* pCmdList,
    _In_ ID3D12Resource* pDestinationResource,
    _In_ ID3D12Resource* pIntermediate,
    UINT64 IntermediateOffset,
    _In_range_(0, D3D12_REQ_SUBRESOURCES) UINT FirstSubresource,
    _In_range_(0, D3D12_REQ_SUBRESOURCES - FirstSubresource) UINT NumSubresources,
    _In_reads_(NumSubresources) D3D12_SUBRESOURCE_DATA* pSrcData)
{
    UINT64 RequiredSize = 0;
    UINT64 MemToAlloc = static_cast<UINT64>(sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT) + sizeof(UINT) + sizeof(UINT64)) * NumSubresources;
    if (MemToAlloc > SIZE_MAX)
    {
        return 0;
    }
    void* pMem = HeapAlloc(GetProcessHeap(), 0, static_cast<SIZE_T>(MemToAlloc));
    if (pMem == nullptr)
    {
        return 0;
    }
    auto pLayouts = reinterpret_cast<D3D12_PLACED_SUBRESOURCE_FOOTPRINT*>(pMem);
    UINT64* pRowSizesInBytes = reinterpret_cast<UINT64*>(pLayouts + NumSubresources);
    UINT* pNumRows = reinterpret_cast<UINT*>(pRowSizesInBytes + NumSubresources);

    auto Desc = pDestinationResource->GetDesc();
    pDevice->GetCopyableFootprints(&Desc, FirstSubresource, NumSubresources, IntermediateOffset, pLayouts, pNumRows, pRowSizesInBytes, &RequiredSize);

    UINT64 Result = UpdateSubresources(pCmdList, pDestinationResource, pIntermediate, FirstSubresource, NumSubresources, RequiredSize, pLayouts, pNumRows, pRowSizesInBytes, pSrcData);
    HeapFree(GetProcessHeap(), 0, pMem);
    return Result;
}

void DirectMLSuperResolutionPass::CreateWeightTensors(
    ID3D12GraphicsCommandList& commandList,
    WeightMapType& weights,
    const char* convLayerName,
    const char* scaleLayerName,
    const char* shiftLayerName,
    std::span<const uint32_t> filterSizes,
    //DirectX::ResourceUploadBatch& uploadBatch,
    _Out_writes_(1) ID3D12Resource** filterWeightResourceOut,
    _Out_writes_opt_(1) ID3D12Resource** biasWeightResourceOut)
{
    // There are two types of weights for the convolutions: The convolution filters themselves, and scale/shift
    // weights used to normalize and bias the results. The final layer doesn't use scale and shift weights, so
    // these are optional.

    bool useScaleShift = true;
    if (scaleLayerName == nullptr)
    {
        assert(shiftLayerName == nullptr);
        useScaleShift = false;
    }

    CreateWeightResource(filterSizes.data(), filterWeightResourceOut);
    if (useScaleShift)
    {
        uint32_t biasSizes[] = { 1, filterSizes[0], 1, 1 };	// One bias per output channel
        CreateWeightResource(biasSizes, biasWeightResourceOut);

        // The scale weights will be premultiplied into the filter weights, so they don't need
        // a separate resource.
    }
    else
    {
        if (biasWeightResourceOut)
            biasWeightResourceOut = nullptr;
    }

    // Convert weight values to FP16
    WeightsType filterWeights = weights[convLayerName];
    WeightsType scaleWeights, shiftWeights;
    if (useScaleShift)
    {
        scaleWeights = weights[scaleLayerName];
        shiftWeights = weights[shiftLayerName];
    }

    std::vector<uint16_t> filterWeightsFP16;
    std::vector<uint16_t> biasWeightsFP16;

    const uint32_t N = filterSizes[0];
    const uint32_t C = filterSizes[1];
    const uint32_t H = filterSizes[2];
    const uint32_t W = filterSizes[3];

    for (uint32_t n = 0; n < N; n++)
    {
        switch (m_tensorLayout)
        {
        case TensorLayout::NHWC:
            // We need to convert the weights from NCHW to NHWC.
            for (uint32_t h = 0; h < H; h++)
                for (uint32_t w = 0; w < W; w++)
                    for (uint32_t c = 0; c < C; c++)
                    {
                        // Apply the scale weight now so we don't need a normalization layer
                        uint32_t idx = w + h * W + c * H * W + n * C * H * W;
                        float scaledWeight = useScaleShift ?
                            filterWeights[idx] * scaleWeights[n] :
                            filterWeights[idx];
                        filterWeightsFP16.push_back(Float16Compressor::compress(scaledWeight));
                    }
            break;

        default:
            // Weights are already in the right order
            for (uint32_t i = 0; i < C * H * W; i++)
            {
                // Apply the scale weight now so we don't need a normalization layer
                uint32_t idx = n * C * H * W + i;
                float scaledWeight = useScaleShift ?
                    filterWeights[idx] * scaleWeights[n] :
                    filterWeights[idx];
                filterWeightsFP16.push_back(Float16Compressor::compress(scaledWeight));
            }
        }

        if (useScaleShift)
        {
            // Technically this is initialBias*scale+shift, but the initial bias is 0
            biasWeightsFP16.push_back(Float16Compressor::compress(shiftWeights[n]));
        }
    }

    // Upload to the GPU
    D3D12_SUBRESOURCE_DATA weightsData = {};
    weightsData.pData = filterWeightsFP16.data(); 

    ComPtr<ID3D12Resource> pFilterWeightsUploadBuffer;
    const D3D12_HEAP_PROPERTIES uploadHeapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(filterWeightsFP16.size() * sizeof(filterWeightsFP16[0]));

    VERIFY_HRESULT(m_device.CreateCommittedResource(
        &uploadHeapDesc,
        D3D12_HEAP_FLAG_NONE,
        &uploadBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_GRAPHICS_PPV_ARGS(pFilterWeightsUploadBuffer.ReleaseAndGetAddressOf())));
    m_uploadResources.push_back(pFilterWeightsUploadBuffer);


    UpdateSubresourcesHelper(&m_device, &commandList,
        *filterWeightResourceOut, pFilterWeightsUploadBuffer.Get(),
        0, 0, 1,
        &weightsData);

    if (useScaleShift)
    {
        weightsData.pData = biasWeightsFP16.data();

        D3D12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(biasWeightsFP16.size() * sizeof(biasWeightsFP16[0]));

        ComPtr<ID3D12Resource> pBiasWeightsUploadBuffer;
        VERIFY_HRESULT(m_device.CreateCommittedResource(
            &uploadHeapDesc,
            D3D12_HEAP_FLAG_NONE,
            &uploadBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_GRAPHICS_PPV_ARGS(pBiasWeightsUploadBuffer.ReleaseAndGetAddressOf())));
        m_uploadResources.push_back(pBiasWeightsUploadBuffer);

        UpdateSubresourcesHelper(&m_device, &commandList,
            *biasWeightResourceOut, pBiasWeightsUploadBuffer.Get(),
            0, 0, 1,
            &weightsData);
    }
}


void DirectMLSuperResolutionPass::CreateWeightResource(
    _In_reads_(4) const uint32_t* tensorSizes,
    _Out_writes_(1) ID3D12Resource** d3dResourceOut)
{
    uint32_t strides[4];
    GetStrides(tensorSizes, m_tensorLayout, strides);
    uint64_t bufferSize = DMLCalcBufferTensorSize(DML_TENSOR_DATA_TYPE_FLOAT16, 4, tensorSizes, strides);

    D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    auto heapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    VERIFY_HRESULT(m_device.CreateCommittedResource(
        &heapDesc,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(d3dResourceOut)
    ));
}
#endif