#include "pch.h"

#if USE_OIDN

#define DISABLE_META_COMMANDS 0
#if DISABLE_META_COMMANDS
#define DML_OPERATOR_FLAGS DML_EXECUTION_FLAG_DISABLE_META_COMMANDS
#else
#define DML_OPERATOR_FLAGS DML_EXECUTION_FLAG_NONE
#endif

#include "TensorToImageCS.h"
#include "ImageToTensorCS.h"
#include "DirectMLSharedShaderStructs.h"

// https://gist.github.com/rygorous/2156668
namespace
{
    typedef unsigned int uint;

    union FP32
    {
        uint u;
        float f;
        struct
        {
            uint Mantissa : 23;
            uint Exponent : 8;
            uint Sign : 1;
        };
    };

    union FP16
    {
        unsigned short u;
        struct
        {
            uint Mantissa : 10;
            uint Exponent : 5;
            uint Sign : 1;
        };
    };

    // Original ISPC reference version; this always rounds ties up.
    FP16 float_to_half(FP32 f)
    {
        FP16 o = { 0 };

        // Based on ISPC reference code (with minor modifications)
        if (f.Exponent == 0) // Signed zero/denormal (which will underflow)
            o.Exponent = 0;
        else if (f.Exponent == 255) // Inf or NaN (all exponent bits set)
        {
            o.Exponent = 31;
            o.Mantissa = f.Mantissa ? 0x200 : 0; // NaN->qNaN and Inf->Inf
        }
        else // Normalized number
        {
            // Exponent unbias the single, then bias the halfp
            int newexp = f.Exponent - 127 + 15;
            if (newexp >= 31) // Overflow, return signed infinity
                o.Exponent = 31;
            else if (newexp <= 0) // Underflow
            {
                if ((14 - newexp) <= 24) // Mantissa might be non-zero
                {
                    uint mant = f.Mantissa | 0x800000; // Hidden 1 bit
                    o.Mantissa = mant >> (14 - newexp);
                    if ((mant >> (13 - newexp)) & 1) // Check for rounding
                        o.u++; // Round, might overflow into exp bit, but this is OK
                }
            }
            else
            {
                o.Exponent = newexp;
                o.Mantissa = f.Mantissa >> 13;
                if (f.Mantissa & 0x1000) // Check for rounding
                    o.u++; // Round, might overflow to inf, this is OK
            }
        }

        o.Sign = f.Sign;
        return o;
    }

    FP32 half_to_float(FP16 h)
    {
        static const FP32 magic = { 113 << 23 };
        static const uint shifted_exp = 0x7c00 << 13; // exponent mask after shift
        FP32 o;

        o.u = (h.u & 0x7fff) << 13;     // exponent/mantissa bits
        uint exp = shifted_exp & o.u;   // just the exponent
        o.u += (127 - 15) << 23;        // exponent adjust

        // handle exponent special cases
        if (exp == shifted_exp) // Inf/NaN?
            o.u += (128 - 16) << 23;    // extra exp adjust
        else if (exp == 0) // Zero/Denormal?
        {
            o.u += 1 << 23;             // extra exp adjust
            o.f -= magic.f;             // renormalize
        }

        o.u |= (h.u & 0x8000) << 16;    // sign bit
        return o;
    }
}

float half_to_float(int16_t x)
{
    FP16 fp16;
    fp16.u = (unsigned short)x;
    return half_to_float(fp16).f;
}

int16_t float_to_half(float x)
{
    FP32 fp32;
    fp32.f = x;
    return (int16_t)float_to_half(fp32).u;
}

// Tensor dimensions
// Canonical order: CHW / OIHW
using TensorDims = std::vector<int>;

std::ostream& operator <<(std::ostream& sm, const TensorDims& dims);

// Tensor memory layout
enum class TensorLayout
{
    x,

    chw,
    Chw8c,  // blocked
    Chw16c, // blocked
    oihw,
    OIhw8i8o,     // blocked
    OIhw16i16o,   // blocked
    OIhw2o8i8o2i, // blocked (Xe-HPG DPAS)
    OIhw8i16o2i,  // blocked (Xe-HPC DPAS)

    hwc,
    ohwi,
};

struct TensorLayoutInfo
{
    int rank;
    int blockC;
};

// Returns information about the tensor layout
inline TensorLayoutInfo getTensorLayoutInfo(TensorLayout layout)
{
    switch (layout)
    {
    case TensorLayout::x:
        return { 1, 1 };
    case TensorLayout::chw:
    case TensorLayout::hwc:
        return { 3, 1 };
    case TensorLayout::Chw8c:
        return { 3, 8 };
    case TensorLayout::Chw16c:
        return { 3, 16 };
    case TensorLayout::oihw:
    case TensorLayout::ohwi:
        return { 4, 1 };
    case TensorLayout::OIhw8i8o:
        return { 4, 8 };
    case TensorLayout::OIhw16i16o:
    case TensorLayout::OIhw2o8i8o2i:
    case TensorLayout::OIhw8i16o2i:
        return { 4, 16 };
    default:
        throw std::invalid_argument("invalid tensor layout");
    }
}

// Data types sorted by precision in ascending order
enum class DataType
{
    Void,
    UInt8,
    Float16,
    Float32,
};

size_t getDataTypeSize(DataType dataType)
{
    switch (dataType)
    {
    case DataType::UInt8:   return 1;
    case DataType::Float16: return sizeof(int16_t);
    case DataType::Float32: return sizeof(float);
    default:
        throw std::invalid_argument("invalid data type");
    }
}

// Tensor descriptor
struct TensorDesc
{
    TensorDims   dims;       // logical dimensions
    TensorDims   paddedDims; // storage dimensions with zero-padding
    TensorLayout layout;     // storage layout
    DataType     dataType;   // element data type

    TensorDesc() = default;

    TensorDesc(TensorDims dims, TensorDims paddedDims, TensorLayout layout, DataType dataType)
        : dims(dims), paddedDims(paddedDims), layout(layout), dataType(dataType)
    {
        assert(isValid());
    }

    TensorDesc(TensorDims dims, TensorLayout layout, DataType dataType)
        : dims(dims), paddedDims(dims), layout(layout), dataType(dataType)
    {
        assert(isValid());
    }

    bool isValid() const
    {
        const auto info = getTensorLayoutInfo(layout);

        return getRank() == info.rank &&
            dims.size() == paddedDims.size() &&
            std::mismatch(dims.begin(), dims.end(), paddedDims.begin(),
                [](int a, int b) { return a <= b; }).first == dims.end() &&
            (info.blockC == 1 ||
                (getRank() == 3 && getPaddedC() % info.blockC == 0) ||
                (getRank() == 4 && getPaddedO() % info.blockC == 0 && getPaddedI() % info.blockC == 0));
    }

    // Returns the number of dimensions
    inline int getRank() const { return int(dims.size()); }

    // Returns the number of elements in a 1D tensor
    inline int getX() const
    {
        assert(dims.size() == 1);
        return dims[0];
    }

    inline int getPaddedX() const
    {
        assert(paddedDims.size() == 1);
        return paddedDims[0];
    }

    // Returns the number of output channels in the tensor
    inline int getO() const
    {
        assert(dims.size() >= 4);
        return dims[dims.size() - 4];
    }

    inline int getPaddedO() const
    {
        assert(paddedDims.size() >= 4);
        return paddedDims[paddedDims.size() - 4];
    }

    // Returns the number of input channels in the tensor
    inline int getI() const
    {
        assert(dims.size() >= 3);
        return dims[dims.size() - 3];
    }

    inline int getPaddedI() const
    {
        assert(paddedDims.size() >= 3);
        return paddedDims[paddedDims.size() - 3];
    }

    // Returns the number of channels in the tensor
    inline int getC() const
    {
        assert(dims.size() >= 3);
        return dims[dims.size() - 3];
    }

    inline int getPaddedC() const
    {
        assert(paddedDims.size() >= 3);
        return paddedDims[paddedDims.size() - 3];
    }

    // Returns the height of the tensor
    inline int getH() const
    {
        assert(dims.size() >= 2);
        return dims[dims.size() - 2];
    }

    // Returns the width of the tensor
    inline int getW() const
    {
        assert(dims.size() >= 2);
        return dims[dims.size() - 1];
    }

    // Returns the number of elements in the tensor
    inline size_t getNumElements() const
    {
        if (dims.empty())
            return 0;
        size_t num = 1;
        for (size_t i = 0; i < dims.size(); ++i)
            num *= size_t(dims[i]);
        return num;
    }

    // Returns the size in bytes of the tensor
    inline size_t getByteSize() const
    {
        if (paddedDims.empty())
            return 0;
        size_t num = 1;
        for (size_t i = 0; i < paddedDims.size(); ++i)
            num *= size_t(paddedDims[i]);
        return num * getDataTypeSize(dataType);
    }

    bool operator ==(const TensorDesc& other) const
    {
        return (dims == other.dims) && (paddedDims == other.paddedDims) &&
            (layout == other.layout) && (dataType == other.dataType);
    }

    bool operator !=(const TensorDesc& other) const
    {
        return (dims != other.dims) || (paddedDims != other.paddedDims) ||
            (layout != other.layout) || (dataType != other.dataType);
    }
};


// Checks for buffer overrun
void checkBounds(const char* ptr, const char* end, size_t size)
{
    if (end - ptr < (ptrdiff_t)size)
        HANDLE_FAILURE(); // invalid or corrupted weights blob
}

// Reads a value from a buffer (with bounds checking) and advances the pointer
template<typename T>
T read(const char*& ptr, const char* end)
{
    checkBounds(ptr, end, sizeof(T));
    T value;
    memcpy(&value, ptr, sizeof(T));
    ptr += sizeof(T);
    return value;
}

class Tensor : protected TensorDesc
{
public:
    virtual void* getData() = 0;
    virtual const void* getData() const = 0;

    inline const TensorDesc& getDesc() const { return *this; }
    inline const TensorDims& getDims() const { return dims; }
    inline TensorLayout getLayout() const { return layout; }
    inline DataType getDataType() const { return dataType; }

    using TensorDesc::getRank;
    using TensorDesc::getX;
    using TensorDesc::getPaddedX;
    using TensorDesc::getO;
    using TensorDesc::getPaddedO;
    using TensorDesc::getI;
    using TensorDesc::getPaddedI;
    using TensorDesc::getC;
    using TensorDesc::getPaddedC;
    using TensorDesc::getH;
    using TensorDesc::getW;
    using TensorDesc::getNumElements;
    using TensorDesc::getByteSize;

    inline operator bool() const { return getData() != nullptr; }

protected:
    explicit Tensor(const TensorDesc& desc);
};

Tensor::Tensor(const TensorDesc& desc)
    : TensorDesc(desc)
{
    assert(desc.isValid());
}

class HostTensor final : public Tensor
{
public:
    explicit HostTensor(const TensorDesc& desc);
    HostTensor(const TensorDesc& desc, void* data);
    ~HostTensor();

    void* getData() override { return ptr; }
    const void* getData() const override { return ptr; }

private:
    void* ptr;   // pointer to the tensor data
    bool shared; // data owned and shared by the user
};

constexpr size_t memoryAlignment = 128;
void* alignedMalloc(size_t size, size_t alignment = memoryAlignment);

void* alignedMalloc(size_t size, size_t alignment)
{
    if (size == 0)
        return nullptr;

    assert((alignment & (alignment - 1)) == 0);
    void* ptr = _mm_malloc(size, alignment);

    if (ptr == nullptr)
        throw std::bad_alloc();

    return ptr;
}

void alignedFree(void* ptr)
{
    if (ptr)
        _mm_free(ptr);
}

// -----------------------------------------------------------------------------------------------
// HostTensor
// -----------------------------------------------------------------------------------------------

HostTensor::HostTensor(const TensorDesc& desc)
    : Tensor(desc),
    ptr(alignedMalloc(getByteSize())),
    shared(false) {}

HostTensor::HostTensor(const TensorDesc& desc, void* data)
    : Tensor(desc),
    ptr(data),
    shared(true) {}

HostTensor::~HostTensor()
{
    if (!shared)
        alignedFree(ptr);
}

using TensorMap = std::unordered_map<std::string, std::shared_ptr<Tensor>>;

void parseTZA(const void* buffer, size_t size, TensorMap &tensorMap)
{
    const char* input = static_cast<const char*>(buffer);
    const char* const bufferEnd = input + size;

    // Parse the magic value
    const int magic = read<uint16_t>(input, bufferEnd);
    if (magic != 0x41D7)
        HANDLE_FAILURE(); // invalid or corrupted weights blob

    // Parse the version
    const int majorVersion = read<uint8_t>(input, bufferEnd);
    const int minorVersion = read<uint8_t>(input, bufferEnd);
    if (majorVersion != 2)
        HANDLE_FAILURE(); // unsupported weights blob version

    // Parse the table offset and jump to the table
    const uint64_t tableOffset = read<uint64_t>(input, bufferEnd);
    input = static_cast<const char*>(buffer) + tableOffset;

    // Parse the number of tensors
    const size_t numTensors = read<uint32_t>(input, bufferEnd);

    // Parse the tensors
    for (size_t i = 0; i < numTensors; ++i)
    {
        TensorDesc tensorDesc;

        // Parse the name
        const size_t nameLen = read<uint16_t>(input, bufferEnd);
        checkBounds(input, bufferEnd, nameLen);
        std::string name(input, nameLen);
        input += nameLen;

        // Parse the number of dimensions
        const int ndims = read<uint8_t>(input, bufferEnd);

        // Parse the shape of the tensor
        tensorDesc.dims.resize(ndims);
        for (int j = 0; j < ndims; ++j)
            tensorDesc.dims[j] = read<uint32_t>(input, bufferEnd);
        tensorDesc.paddedDims = tensorDesc.dims;

        // Parse the layout of the tensor
        checkBounds(input, bufferEnd, ndims);
        std::string layout = std::string(input, input + ndims);
        if (layout == "x")
            tensorDesc.layout = TensorLayout::x;
        else if (layout == "oihw")
            tensorDesc.layout = TensorLayout::oihw;
        else
            HANDLE_FAILURE(); // invalid tensor layout");
        input += ndims;

        // Parse the data type of the tensor
        const char dataType = read<char>(input, bufferEnd);
        if (dataType == 'f')
            tensorDesc.dataType = DataType::Float32;
        else if (dataType == 'h')
            tensorDesc.dataType = DataType::Float16;
        else
            HANDLE_FAILURE(); // invalid tensor data type

        // Parse the offset to the tensor data
        const uint64_t tensorOffset = read<uint64_t>(input, bufferEnd);
        const char* tensorData = static_cast<const char*>(buffer) + tensorOffset;
        checkBounds(tensorData, bufferEnd, tensorDesc.getByteSize());

        // Add the tensor to the map
        auto tensor = std::make_shared<HostTensor>(tensorDesc, const_cast<char*>(tensorData));
        tensorMap.emplace(name, tensor);
    }
}


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

OpenImageDenoise::OpenImageDenoise(ID3D12Device& device) :
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

void LoadBuffer(std::wstring fileName, std::vector<BYTE>& buffer)
{
	std::ifstream file(fileName, std::ios::binary | std::ios::ate); 
    if (!file.is_open())
    {
		throw std::exception("Failed to open file");
	}
    size_t bufferSize = file.tellg();
    buffer.resize(bufferSize);

	file.seekg(0, std::ios::beg);
	file.read((char*)buffer.data(), bufferSize);
	file.close();
}

void OpenImageDenoise::OnResize(
    ID3D12GraphicsCommandList& commandList,
    CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHeapCPUBase,
    CD3DX12_GPU_DESCRIPTOR_HANDLE descriptorHeapGPUBase,
    UINT descriptorSize,
    UINT Width,
    UINT Height,
    float& OutDownscaleFactor)
{
    ResetDescriptorTable(descriptorHeapCPUBase, descriptorHeapGPUBase, descriptorSize);

    TensorMap tensorMap;
    std::vector<BYTE> buffer;
    LoadBuffer(L"rt_ldr.tza", buffer);
    parseTZA(buffer.data(), buffer.size(), tensorMap);
    
#if 0
    // Create the model graph
    auto inputProcess = graph->addInputProcess("input", inputDims, tileAlignment, transferFunc, hdr, snorm);

    auto encConv0 = graph->addConv("enc_conv0", inputProcess, Activation::ReLU);

    auto pool1 = graph->addConv("enc_conv1", encConv0, Activation::ReLU, PostOp::Pool);

    auto pool2 = graph->addConv("enc_conv2", pool1, Activation::ReLU, PostOp::Pool);

    auto pool3 = graph->addConv("enc_conv3", pool2, Activation::ReLU, PostOp::Pool);

    auto pool4 = graph->addConv("enc_conv4", pool3, Activation::ReLU, PostOp::Pool);

    auto encConv5a = graph->addConv("enc_conv5a", pool4, Activation::ReLU);

    auto upsample4 = graph->addConv("enc_conv5b", encConv5a, Activation::ReLU, PostOp::Upsample);
    auto decConv4a = graph->addConcatConv("dec_conv4a", upsample4, pool3, Activation::ReLU);

    auto upsample3 = graph->addConv("dec_conv4b", decConv4a, Activation::ReLU, PostOp::Upsample);
    auto decConv3a = graph->addConcatConv("dec_conv3a", upsample3, pool2, Activation::ReLU);

    auto upsample2 = graph->addConv("dec_conv3b", decConv3a, Activation::ReLU, PostOp::Upsample);
    auto decConv2a = graph->addConcatConv("dec_conv2a", upsample2, pool1, Activation::ReLU);

    auto upsample1 = graph->addConv("dec_conv2b", decConv2a, Activation::ReLU, PostOp::Upsample);
    auto decConv1a = graph->addConcatConv("dec_conv1a", upsample1, inputProcess, Activation::ReLU);
    auto decConv1b = graph->addConv("dec_conv1b", decConv1a, Activation::ReLU);

    auto decConv0 = graph->addConv("dec_conv0", decConv1b, Activation::ReLU);

    auto outputProcess = graph->addOutputProcess("output", decConv0, transferFunc, hdr, snorm);

#endif

    OutDownscaleFactor = 1.0f;
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

    uint32_t intermediateInputSizes[2][4];
    uint32_t passIndex = 0;
    
    DirectMLPass ModelInputPass = {};
    ModelInputPass.m_OutputHeight = ModelInputPass.m_InputHeight = Height;
    ModelInputPass.m_OutputWidth = ModelInputPass.m_InputWidth = Width;
    ModelInputPass.m_InputChannelDepth = ModelInputPass.m_OutputChannelDepth = 3;

    m_ConvolutionPasses[passIndex++] = CreateConvolutionLayer(commandList, *tensorMap["enc_conv0.weight"].get(), *tensorMap["enc_conv0.bias"].get(), ModelInputPass, &modelInputBufferSize,
        &intermediateBufferMaxSize[0], intermediateInputSizes[0], &m_dmlConvOps[0]);
    m_DMLPasses.push_back(&m_ConvolutionPasses[passIndex - 1]);

    // Which intermediate resource to use as input for the current operation. The other will be
    // used as output. Then the next op will swap the order.
    m_ConvolutionPasses[passIndex++] = CreateConvolutionLayer(commandList, *tensorMap["enc_conv1.weight"].get(), *tensorMap["enc_conv1.bias"].get(), *m_DMLPasses.back(), &intermediateBufferMaxSize[0],
        &intermediateBufferMaxSize[1], intermediateInputSizes[1], &m_dmlConvOps[1]);
    m_DMLPasses.push_back(&m_ConvolutionPasses[passIndex - 1]);
    //inputIndex = 1 - inputIndex;

    uint32_t poolingPassIndex = 0;
    m_PoolingPass[poolingPassIndex++] = CreatePoolingLayer(intermediateInputSizes[1], &m_dmlPoolingOps[0]);
    m_DMLPasses.push_back(&m_PoolingPass[poolingPassIndex - 1]);
    
#if 0
    m_ConvolutionPasses[passIndex++] = CreateConvolutionLayer(commandList, *tensorMap["enc_conv2.weight"].get(), *tensorMap["enc_conv2.bias"].get(), *m_DMLPasses.back(), nullptr,
        &intermediateBufferMaxSize[1], intermediateInputSizes[1], &m_dmlConvOps[2]);
    m_DMLPasses.push_back(&m_ConvolutionPasses[passIndex - 1]);

    CreateWeightTensors(commandList, weights, "conv1/weights", "conv1/BatchNorm/scale", "conv1/BatchNorm/shift",
        filterSizes1, &m_modelConvFilterWeights[0], &m_modelConvBiasWeights[0]);
#endif

#if 0
    // Which intermediate resource to use as input for the current operation. The other will be
    // used as output. Then the next op will swap the order.
    int inputIndex = 0;

    uint32_t const filterSizes2[] = { 64, 32, 3, 3 };	// output filters
    m_ConvolutionPasses[passIndex++] = CreateConvolutionLayer(intermediateInputSizes[inputIndex], filterSizes2, true, &intermediateBufferMaxSize[inputIndex],
        &intermediateBufferMaxSize[1 - inputIndex], intermediateInputSizes[1 - inputIndex], &m_dmlConvOps[1]);
    CreateWeightTensors(commandList, weights, "conv2/weights", "conv2/BatchNorm/scale", "conv2/BatchNorm/shift",
        filterSizes2, &m_modelConvFilterWeights[1], &m_modelConvBiasWeights[1]);
    inputIndex = 1 - inputIndex;

    uint32_t const filterSizes3[] = { 64, 64, 3, 3 };
    m_ConvolutionPasses[passIndex++] = CreateConvolutionLayer(intermediateInputSizes[inputIndex], filterSizes3, true, &intermediateBufferMaxSize[inputIndex],
        &intermediateBufferMaxSize[1 - inputIndex], intermediateInputSizes[1 - inputIndex], &m_dmlConvOps[2]);
    CreateWeightTensors(commandList, weights, "conv3/weights", "conv3/BatchNorm/scale", "conv3/BatchNorm/shift",
        filterSizes3, &m_modelConvFilterWeights[2], &m_modelConvBiasWeights[2]);
    inputIndex = 1 - inputIndex;

    CreateUpsampleLayer(intermediateInputSizes[inputIndex], &intermediateBufferMaxSize[inputIndex],
        &intermediateBufferMaxSize[1 - inputIndex], intermediateInputSizes[1 - inputIndex], &m_dmlUpsampleOps[1]);
    inputIndex = 1 - inputIndex;

    uint32_t const filterSizes4[] = { 32, 64, 5, 5 };
    m_ConvolutionPasses[passIndex++] = CreateConvolutionLayer(intermediateInputSizes[inputIndex], filterSizes4, true, &intermediateBufferMaxSize[inputIndex],
        &intermediateBufferMaxSize[1 - inputIndex], intermediateInputSizes[1 - inputIndex], &m_dmlConvOps[3]);
    CreateWeightTensors(commandList, weights, "conv_up1/conv/weights", "conv_up1/conv/BatchNorm/scale", "conv_up1/conv/BatchNorm/shift",
        filterSizes4, &m_modelConvFilterWeights[3], &m_modelConvBiasWeights[3]);
    inputIndex = 1 - inputIndex;

    uint32_t const filterSizes5[] = { 32, 32, 3, 3 };
    m_ConvolutionPasses[passIndex++] = CreateConvolutionLayer(intermediateInputSizes[inputIndex], filterSizes5, true, &intermediateBufferMaxSize[inputIndex],
        &intermediateBufferMaxSize[1 - inputIndex], intermediateInputSizes[1 - inputIndex], &m_dmlConvOps[4]);
    CreateWeightTensors(commandList, weights, "conv4/weights", "conv4/BatchNorm/scale", "conv4/BatchNorm/shift",
        filterSizes5, &m_modelConvFilterWeights[4], &m_modelConvBiasWeights[4]);
    inputIndex = 1 - inputIndex;

    m_ConvolutionPasses[passIndex++] = CreateConvolutionLayer(intermediateInputSizes[inputIndex], filterSizes5, true, &intermediateBufferMaxSize[inputIndex],
        &intermediateBufferMaxSize[1 - inputIndex], intermediateInputSizes[1 - inputIndex], &m_dmlConvOps[5]);
    CreateWeightTensors(commandList, weights, "conv5/weights", "conv5/BatchNorm/scale", "conv5/BatchNorm/shift",
        filterSizes5, &m_modelConvFilterWeights[5], &m_modelConvBiasWeights[5]);
    inputIndex = 1 - inputIndex;

    uint32_t const filterSizes6[] = { 3, 32, 3, 3 };
    m_ConvolutionPasses[passIndex++] = CreateConvolutionLayer(intermediateInputSizes[inputIndex], filterSizes6, false, &intermediateBufferMaxSize[inputIndex],
        &intermediateBufferMaxSize[1 - inputIndex], intermediateInputSizes[1 - inputIndex], &m_dmlConvOps[6]);
    CreateWeightTensors(commandList, weights, "conv6/weights", nullptr, nullptr, filterSizes6,
        &m_modelConvFilterWeights[6], nullptr);
    inputIndex = 1 - inputIndex;

    VERIFY(passIndex == c_numConvLayers);
#endif

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

    //size_t upsampleOpDescriptorCount;
    //size_t upsampleDescriptorsIdx;

    //VERIFY_HRESULT(m_pDMLDevice->CreateOperatorInitializer(c_numUpsampleLayers, m_dmlUpsampleOps[0].GetAddressOf(), IID_PPV_ARGS(m_dmlOpInitializers[e_opUpsample].GetAddressOf())));
    //upsampleOpDescriptorCount = GetDescriptorCount(c_numUpsampleLayers, m_dmlUpsampleOps[0].GetAddressOf(), m_dmlOpInitializers[e_opUpsample].Get());

    VERIFY_HRESULT(m_pDMLDevice->CreateOperatorInitializer(c_numConvLayers, m_dmlConvOps[0].GetAddressOf(), IID_PPV_ARGS(m_dmlOpInitializers[e_opConv].GetAddressOf())));
    size_t convOpDescriptorCount = GetDescriptorCount(c_numConvLayers, m_dmlConvOps[0].GetAddressOf(), m_dmlOpInitializers[e_opConv].Get());
    size_t convOpInitializerDescriptorCount = m_dmlOpInitializers[e_opConv]->GetBindingProperties().RequiredDescriptorCount;

    VERIFY_HRESULT(m_pDMLDevice->CreateOperatorInitializer(c_numPoolingLayers, m_dmlPoolingOps[0].GetAddressOf(), IID_PPV_ARGS(m_dmlOpInitializers[e_opPooling].GetAddressOf())));

    auto modelInputDescriptorSlot = AllocateDescriptor();

    // Describe and create a UAV for the original input tensor.
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R16_FLOAT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = static_cast<UINT>(modelInputBufferSize / sizeof(uint16_t));
    uavDesc.Buffer.StructureByteStride = 0;
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    m_device.CreateUnorderedAccessView(m_modelInput.Get(), nullptr, &uavDesc, modelInputDescriptorSlot.m_cpuHandle);
    m_modelInputUAV = modelInputDescriptorSlot.m_gpuHandle;

    auto modelOutputDescriptorSlot = AllocateDescriptor();

    // Describe and create a SRV for the final result tensor.
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R16_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = static_cast<UINT>(modelOutputBufferSize / sizeof(uint16_t));
    srvDesc.Buffer.StructureByteStride = 0;
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    m_device.CreateShaderResourceView(m_modelOutput.Get(), &srvDesc, modelOutputDescriptorSlot.m_cpuHandle);
    m_modelOutputSRV = modelOutputDescriptorSlot.m_gpuHandle;

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

        std::wstring name = L"Intermediate Buffer " + std::to_wstring(i);
        m_modelIntermediateResult[i]->SetName(name.c_str());

        srvDesc.Buffer.NumElements = static_cast<UINT>(intermediateBufferMaxSize[i] / sizeof(uint16_t));

        auto intermediateDescriptorRange = AllocateDescriptor();
        m_device.CreateShaderResourceView(m_modelIntermediateResult[i].Get(), &srvDesc, intermediateDescriptorRange.m_cpuHandle);
        m_modelIntermediateSRV[i] = intermediateDescriptorRange.m_gpuHandle;
    }

    // Create any persistent resources required for the operators.
    {
        for (int i = 0; i < c_numConvLayers + c_numPoolingLayers; i++)
        {
            IDMLCompiledOperator* currentOp;
            ID3D12Resource** persistentResource;
     
            if (i < c_numPoolingLayers)
            {
                currentOp = m_dmlPoolingOps[i].Get();
                persistentResource = m_modelPoolingPersistentResources[i].ReleaseAndGetAddressOf();
            }
            else 
            {
                currentOp = m_dmlConvOps[i - c_numPoolingLayers].Get();
                persistentResource = m_modelConvPersistentResources[i - c_numPoolingLayers].ReleaseAndGetAddressOf();
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

#if 0
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
#endif

    // Convolution layers
    {
        // Bind resources for initialization
        auto bindingProps = m_dmlOpInitializers[e_opConv]->GetBindingProperties();
        assert(bindingProps.PersistentResourceSize == 0);

        auto convInitializerDescriptorTable = AllocateDescriptor(bindingProps.RequiredDescriptorCount);

        DML_BINDING_TABLE_DESC tableDesc = {
            m_dmlOpInitializers[e_opConv].Get(),
            convInitializerDescriptorTable.m_cpuHandle,
            convInitializerDescriptorTable.m_gpuHandle,
            bindingProps.RequiredDescriptorCount
        };

        VERIFY_HRESULT(m_pDMLDevice->CreateBindingTable(&tableDesc, IID_PPV_ARGS(&initBindingTable)));


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
            m_ConvolutionPasses[i].m_OutputSRV = (i == 1 || i == 4 || i == 6) ? m_modelIntermediateSRV[1] : m_modelIntermediateSRV[0];

            bindingProps = m_dmlConvOps[i]->GetBindingProperties();

            auto descriptorTable = AllocateDescriptor(convOpDescriptorCount);

            tableDesc = {
                m_dmlConvOps[i].Get(),
                descriptorTable.m_cpuHandle,
                descriptorTable.m_gpuHandle,
                bindingProps.RequiredDescriptorCount
            };
            VERIFY_HRESULT(m_pDMLDevice->CreateBindingTable(&tableDesc, IID_PPV_ARGS(m_ConvolutionPasses[i].m_pBindingTable.ReleaseAndGetAddressOf())));

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
            auto pFilterWeights = m_ConvolutionPasses[i].m_pFilterWeightResource.Get();
            auto pBiasWeights = m_ConvolutionPasses[i].m_pBiasWeightResource.Get();

            // Bind the weight resources
            DML_BUFFER_BINDING filterBufferBinding = { pFilterWeights, 0, pFilterWeights->GetDesc().Width };
            DML_BINDING_DESC filterBinding = { DML_BINDING_TYPE_BUFFER, &filterBufferBinding };

            DML_BUFFER_BINDING biasBufferBinding = { pBiasWeights, 0, pBiasWeights->GetDesc().Width };
            DML_BINDING_DESC biasBinding = { DML_BINDING_TYPE_BUFFER, &biasBufferBinding };

            DML_BINDING_DESC inputBindings[] = { inputBinding, filterBinding, biasBinding };
#endif
            m_ConvolutionPasses[i].m_pBindingTable->BindInputs(3, inputBindings);
            m_ConvolutionPasses[i].m_pBindingTable->BindOutputs(1, &outputBinding);
            BindTempResourceIfNeeded(m_device, bindingProps, m_ConvolutionPasses[i].GetBindingTable(), m_modelConvTemporaryResources[i].ReleaseAndGetAddressOf());

            if (m_modelConvPersistentResources[i].Get() != nullptr)
                m_ConvolutionPasses[i].m_pBindingTable->BindPersistentResource(&convPersistentBindings[i]);
        }
    }

    // Pooling layers
    {
        // Bind resources for initialization.
        auto bindingProps = m_dmlOpInitializers[e_opPooling]->GetBindingProperties();
        // The DML API guarantees that initialization never uses a persistent resource.
        assert(bindingProps.PersistentResourceSize == 0);

        auto poolingInitializerDescriptorTable = AllocateDescriptor(bindingProps.RequiredDescriptorCount);

        DML_BINDING_TABLE_DESC tableDesc = {
            m_dmlOpInitializers[e_opPooling].Get(),
            poolingInitializerDescriptorTable.m_cpuHandle,
            poolingInitializerDescriptorTable.m_gpuHandle,
            bindingProps.RequiredDescriptorCount
        };
        initBindingTable->Reset(&tableDesc);

        // If the operator requires a persistent resource, it must be bound as output for the initializer.
        DML_BUFFER_BINDING poolingPersistentBuffers[c_numPoolingLayers];
        DML_BINDING_DESC poolingPersistentBindings[c_numPoolingLayers];
        for (int i = 0; i < c_numPoolingLayers; i++)
        {
            if (m_modelPoolingPersistentResources[i].Get() != nullptr)
            {
                poolingPersistentBuffers[i] = { m_modelPoolingPersistentResources[i].Get(), 0, m_modelPoolingPersistentResources[i]->GetDesc().Width };
                poolingPersistentBindings[i] = { DML_BINDING_TYPE_BUFFER, &poolingPersistentBuffers[i] };
            }
            else
                poolingPersistentBindings[i] = emptyBindingDesc;
        }

        // The inputs will vary each frame, so don't bind inputs at initialization.
        initBindingTable->BindInputs(0, nullptr);
        initBindingTable->BindOutputs(c_numPoolingLayers, poolingPersistentBindings);
        BindTempResourceIfNeeded(m_device, bindingProps, initBindingTable.Get(), m_modelInitTemporaryResources[e_opPooling].ReleaseAndGetAddressOf());

        // Run initialization
        m_pCommandRecorder->RecordDispatch(&commandList, m_dmlOpInitializers[e_opPooling].Get(), initBindingTable.Get());

        // Bind resources for execution
        for (int i = 0; i < c_numPoolingLayers; i++)
        {
            PoolingPass &pass = m_PoolingPass[i];

            bindingProps = m_dmlPoolingOps[i]->GetBindingProperties();

            auto descriptorTable = AllocateDescriptor(bindingProps.RequiredDescriptorCount);

            tableDesc = {
                m_dmlPoolingOps[i].Get(),
                descriptorTable.m_cpuHandle,
                descriptorTable.m_gpuHandle,
                bindingProps.RequiredDescriptorCount
            };
            VERIFY_HRESULT(m_pDMLDevice->CreateBindingTable(&tableDesc, IID_PPV_ARGS(m_dmlPoolingBindings[i].ReleaseAndGetAddressOf())));

            auto inputResource = m_modelIntermediateResult[1];
            auto outputResource = pass.m_pOutputResource.Get();

            DML_BUFFER_BINDING inputBufferBinding = { inputResource.Get(), 0, inputResource->GetDesc().Width };
            DML_BINDING_DESC inputBinding = { DML_BINDING_TYPE_BUFFER, &inputBufferBinding };
            DML_BUFFER_BINDING outputBufferBinding = { outputResource, 0, outputResource->GetDesc().Width };
            DML_BINDING_DESC outputBinding = { DML_BINDING_TYPE_BUFFER, &outputBufferBinding };

            m_dmlPoolingBindings[i]->BindInputs(1, &inputBinding);
            m_dmlPoolingBindings[i]->BindOutputs(1, &outputBinding);
            BindTempResourceIfNeeded(m_device, bindingProps, m_dmlPoolingBindings[i].Get(), m_modelPoolingTemporaryResources[i].ReleaseAndGetAddressOf());

            if (m_modelPoolingPersistentResources[i].Get() != nullptr)
                m_dmlPoolingBindings[i]->BindPersistentResource(&poolingPersistentBindings[i]);

            pass.m_pBindingTable = m_dmlPoolingBindings[i];
        }
    }
}

void GetStrides(
    _In_reads_(4) const uint32_t* sizes,
    OpenImageDenoise::TensorLayout layout,
    _Out_writes_(4) uint32_t* stridesOut
)
{
    switch (layout)
    {
    case OpenImageDenoise::TensorLayout::NHWC:
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

OpenImageDenoise::ConvolutionPass OpenImageDenoise::CreateConvolutionLayer(
    ID3D12GraphicsCommandList& commandList,
    const Tensor& weights,
    const Tensor& bias,
    DirectMLPass &pass,
    _Inout_updates_(1) uint64_t* inputBufferRequiredSize,
    _Inout_updates_(1) uint64_t* outputBufferRequiredSize,
    _Out_writes_(4) uint32_t* outputSizesOut,
    _Out_writes_(1) IDMLCompiledOperator** compiledOpOut)
{

    uint32_t inputSizes[] = { 1, pass.m_OutputChannelDepth, pass.m_OutputHeight, pass.m_OutputWidth };

    VERIFY(weights.getLayout() == ::TensorLayout::oihw);
    VERIFY(weights.getDims()[1] == inputSizes[1]);

    bool useBiasAndActivation = true;

    uint32_t filterSizes[4];
    filterSizes[3] = weights.getDims()[3];
    filterSizes[2] = weights.getDims()[2];
    filterSizes[1] = weights.getDims()[1];
    filterSizes[0] = weights.getDims()[0];

    // Describe input and output tensors    
    uint32_t inputStrides[4];
    GetStrides(inputSizes, m_tensorLayout, inputStrides);

    uint64_t inputBufferSize = DMLCalcBufferTensorSize(DML_TENSOR_DATA_TYPE_FLOAT16, 4, inputSizes, inputStrides);
    if (inputBufferRequiredSize)
    {
        *inputBufferRequiredSize = std::max(inputBufferSize, *inputBufferRequiredSize);
    }

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
    VERIFY_HRESULT(m_pDMLDevice->CompileOperator(op.Get(), DML_OPERATOR_FLAGS | DML_EXECUTION_FLAG_ALLOW_HALF_PRECISION_COMPUTATION, IID_PPV_ARGS(compiledOpOut)));

    ConvolutionPass Pass = {};
    Pass.m_InputWidth = Pass.m_OutputWidth = inputSizes[3];
    Pass.m_InputHeight = Pass.m_OutputHeight = inputSizes[2];
    Pass.m_InputChannelDepth = inputSizes[1];
    Pass.m_OutputChannelDepth = outputSizesOut[1];
    Pass.m_pOperator = *compiledOpOut;

    CreateWeightTensors(commandList, weights, bias, filterSizes, Pass.m_pFilterWeightResource.ReleaseAndGetAddressOf(), Pass.m_pBiasWeightResource.ReleaseAndGetAddressOf());

    return Pass;
}

void OpenImageDenoise::CreateUpsampleLayer(
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
    VERIFY_HRESULT(m_pDMLDevice->CompileOperator(op.Get(), DML_OPERATOR_FLAGS | DML_EXECUTION_FLAG_ALLOW_HALF_PRECISION_COMPUTATION, IID_PPV_ARGS(compiledOpOut)));
}


void OpenImageDenoise::CreateAdditionLayer(
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
    VERIFY_HRESULT(m_pDMLDevice->CompileOperator(op.Get(), DML_OPERATOR_FLAGS | DML_EXECUTION_FLAG_ALLOW_HALF_PRECISION_COMPUTATION, IID_PPV_ARGS(compiledOpOut)));
}


OpenImageDenoise::PoolingPass OpenImageDenoise::CreatePoolingLayer(
    _In_reads_(4) const uint32_t* inputSizes,
    _Out_writes_(1) IDMLCompiledOperator** compiledOpOut)
{
    // Describe input and output tensors
    uint32_t inputStrides[4];
    GetStrides(inputSizes, m_tensorLayout, inputStrides);
    uint64_t inputBufferSize = DMLCalcBufferTensorSize(DML_TENSOR_DATA_TYPE_FLOAT16, 4, inputSizes, inputStrides);

    DML_BUFFER_TENSOR_DESC inputBufferDesc = { DML_TENSOR_DATA_TYPE_FLOAT16, DML_TENSOR_FLAG_NONE, 4, inputSizes, inputStrides, inputBufferSize, 0 };
    DML_TENSOR_DESC inputTensorDesc = { DML_TENSOR_TYPE_BUFFER, &inputBufferDesc };

    uint32_t outputSizes[] = {inputSizes[0], inputSizes[1], inputSizes[2] / 2, inputSizes[3] / 2};

    uint32_t outputStrides[4];
    GetStrides(outputSizes, m_tensorLayout, outputStrides);
    uint64_t outputBufferSize = DMLCalcBufferTensorSize(DML_TENSOR_DATA_TYPE_FLOAT16, 4, outputSizes, outputStrides);
    DML_BUFFER_TENSOR_DESC outputBufferDesc = { DML_TENSOR_DATA_TYPE_FLOAT16, DML_TENSOR_FLAG_NONE, 4, outputSizes, outputStrides, outputBufferSize, 0 };
    DML_TENSOR_DESC outputTensorDesc = { DML_TENSOR_TYPE_BUFFER, &outputBufferDesc };

    uint32_t windowSize[] = { 2, 2 };
    uint32_t startPadding[] = { 0, 0 };
    uint32_t endPadding[] = { 0, 0 };
    uint32_t strides[] = { 2, 2 };

    // Describe, create, and compile max pooling operator
    DML_MAX_POOLING_OPERATOR_DESC poolingDesc = {};
    poolingDesc.InputTensor = &inputTensorDesc;
    poolingDesc.OutputTensor = &outputTensorDesc;
    poolingDesc.DimensionCount = 2;
    poolingDesc.Strides = strides;
    poolingDesc.StartPadding = startPadding;
    poolingDesc.EndPadding = endPadding;
    poolingDesc.WindowSize = windowSize;
    DML_OPERATOR_DESC opDesc = { DML_OPERATOR_MAX_POOLING, &poolingDesc };

    ComPtr<IDMLOperator> op;
    VERIFY_HRESULT(m_pDMLDevice->CreateOperator(&opDesc, IID_PPV_ARGS(op.ReleaseAndGetAddressOf())));
    // TODO: How to use meta commands?
    VERIFY_HRESULT(m_pDMLDevice->CompileOperator(op.Get(), DML_OPERATOR_FLAGS | DML_EXECUTION_FLAG_ALLOW_HALF_PRECISION_COMPUTATION, IID_PPV_ARGS(compiledOpOut)));

    PoolingPass Pass = {};
    Pass.m_InputHeight = inputSizes[2];
    Pass.m_InputWidth = inputSizes[3];
    Pass.m_OutputHeight = outputSizes[2];
    Pass.m_OutputWidth = outputSizes[3];
    Pass.m_OutputChannelDepth = Pass.m_InputChannelDepth = inputSizes[1];
    Pass.m_pOperator = *compiledOpOut;

    D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(outputBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    auto heapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    VERIFY_HRESULT(m_device.CreateCommittedResource(
        &heapDesc,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(Pass.m_pOutputResource.ReleaseAndGetAddressOf())
    ));
    Pass.m_pOutputResource->SetName(L"PoolingOutput");

    auto DescriptorTable = AllocateDescriptor();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R16_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = static_cast<UINT>(outputBufferSize / sizeof(uint16_t));
    srvDesc.Buffer.StructureByteStride = 0;
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    m_device.CreateShaderResourceView(Pass.m_pOutputResource.Get(), &srvDesc, DescriptorTable.m_cpuHandle);
    Pass.m_OutputSRV = DescriptorTable.m_gpuHandle;

    return Pass;
}

D3D12_GPU_DESCRIPTOR_HANDLE OpenImageDenoise::Run(
	ID3D12GraphicsCommandList& commandList,
	PassResource OutputBuffer,
	D3D12_GPU_DESCRIPTOR_HANDLE InputTexture,
	UINT inputWidth,
	UINT inputHeight,
    UINT convolutionLayerToDebug,
    UINT sliceToDebug)
{
	PIXScopedEvent(&commandList, PIX_COLOR_DEFAULT, L"Open Image Denoise");

	auto uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);


    // Convert image to tensor format (original texture -> model input)
    {
        PIXScopedEvent(&commandList, PIX_COLOR_DEFAULT, L"Convert input image");
        commandList.SetPipelineState(m_pImageToTensorPSO.Get());
        commandList.SetComputeRootSignature(m_pRootSignature.Get());


        DirectMLConstants constants = {};
        constants.InputResolution = { inputWidth, inputHeight };
        constants.OutputResolution = { inputWidth, inputHeight };
        constants.UseNHWC = (m_tensorLayout == TensorLayout::NHWC);
        constants.SliceToDebug = sliceToDebug;
        commandList.SetComputeRoot32BitConstants(DirectMLSuperResolutionRootSignatureParameters::ConstantsParam, sizeof(constants) / sizeof(UINT32), &constants, 0);

        commandList.SetComputeRootDescriptorTable(DirectMLSuperResolutionRootSignatureParameters::Input, InputTexture);
        commandList.SetComputeRootDescriptorTable(DirectMLSuperResolutionRootSignatureParameters::Output, m_modelInputUAV);

        UINT DispatchWidth = (inputWidth - 1) / DIRECTML_THREAD_GROUP_WIDTH + 1;
        UINT DispatchHeight = (inputHeight - 1) / DIRECTML_THREAD_GROUP_HEIGHT + 1;

        commandList.Dispatch(DispatchWidth, DispatchHeight, 1);
        commandList.ResourceBarrier(1, &uavBarrier);
    }

	// Run the intermediate model steps: 3 convolutions (with premultiplied batch normalization
	// baked into the weights), an upsample, 3 convolutions w/ premultiplied batch norm, 1 final convolution.
	// This generates a residual image.
#if 0
    // Create the model graph
    auto inputProcess = graph->addInputProcess("input", inputDims, tileAlignment, transferFunc, hdr, snorm);

    auto encConv0 = graph->addConv("enc_conv0", inputProcess, Activation::ReLU);

    auto pool1 = graph->addConv("enc_conv1", encConv0, Activation::ReLU, PostOp::Pool);

    auto pool2 = graph->addConv("enc_conv2", pool1, Activation::ReLU, PostOp::Pool);

    auto pool3 = graph->addConv("enc_conv3", pool2, Activation::ReLU, PostOp::Pool);

    auto pool4 = graph->addConv("enc_conv4", pool3, Activation::ReLU, PostOp::Pool)

    auto encConv5a = graph->addConv("enc_conv5a", pool4, Activation::ReLU);

    auto upsample4 = graph->addConv("enc_conv5b", encConv5a, Activation::ReLU, PostOp::Upsample);
    auto decConv4a = graph->addConcatConv("dec_conv4a", upsample4, pool3, Activation::ReLU);

    auto upsample3 = graph->addConv("dec_conv4b", decConv4a, Activation::ReLU, PostOp::Upsample);
    auto decConv3a = graph->addConcatConv("dec_conv3a", upsample3, pool2, Activation::ReLU);

    auto upsample2 = graph->addConv("dec_conv3b", decConv3a, Activation::ReLU, PostOp::Upsample);
    auto decConv2a = graph->addConcatConv("dec_conv2a", upsample2, pool1, Activation::ReLU);

    auto upsample1 = graph->addConv("dec_conv2b", decConv2a, Activation::ReLU, PostOp::Upsample);
    auto decConv1a = graph->addConcatConv("dec_conv1a", upsample1, inputProcess, Activation::ReLU);
    auto decConv1b = graph->addConv("dec_conv1b", decConv1a, Activation::ReLU);

    auto decConv0 = graph->addConv("dec_conv0", decConv1b, Activation::ReLU);
#endif

	for (int i = 0; i < m_DMLPasses.size(); i++)
	{
        DirectMLPass& pass = *m_DMLPasses[i];

		m_pCommandRecorder->RecordDispatch(&commandList, pass.GetOperator(), pass.GetBindingTable());
		commandList.ResourceBarrier(1, &uavBarrier);

        if (i == convolutionLayerToDebug)
        {
            break;
        }
	}


    {
        PIXScopedEvent(&commandList, PIX_COLOR_DEFAULT, L"Render to texture");

        int LastPassIndex = convolutionLayerToDebug < m_DMLPasses.size() ? convolutionLayerToDebug : m_DMLPasses.size() - 1;
        DirectMLPass& pass = *m_DMLPasses[LastPassIndex];

        D3D12_GPU_DESCRIPTOR_HANDLE OutputSRV = pass.m_OutputSRV;
        UINT passWidth = pass.m_OutputWidth;
        UINT passHeight = pass.m_OutputHeight;

        commandList.SetPipelineState(m_pTensorToImagePSO.Get());
        commandList.SetComputeRootSignature(m_pRootSignature.Get());

        UINT outputWidth = inputWidth;
        UINT outputHeight = inputHeight;

        DirectMLConstants constants = {};
        constants.InputResolution = { passWidth, passHeight };
        constants.OutputResolution = { outputWidth, outputHeight };
        constants.UseNHWC = (m_tensorLayout == TensorLayout::NHWC);
        constants.SliceToDebug = sliceToDebug;
        commandList.SetComputeRoot32BitConstants(DirectMLSuperResolutionRootSignatureParameters::ConstantsParam, sizeof(constants) / sizeof(UINT32), &constants, 0);

        commandList.SetComputeRootDescriptorTable(DirectMLSuperResolutionRootSignatureParameters::Input, OutputSRV);
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

void OpenImageDenoise::CreateWeightTensors(
    ID3D12GraphicsCommandList& commandList,
    const Tensor& weights,
    const Tensor& bias,
    std::span<const uint32_t> filterSizes,
    _Out_writes_(1) ID3D12Resource** filterWeightResourceOut,
    _Out_writes_opt_(1) ID3D12Resource** biasWeightResourceOut)
{
    VERIFY(weights.getDataType() == DataType::Float16);
    VERIFY(bias.getDataType() == DataType::Float16);
    VERIFY(weights.getLayout() == ::TensorLayout::oihw);
    VERIFY(bias.getLayout() == ::TensorLayout::x);

    bool bHasBias = true;
    CreateWeightResource(filterSizes.data(), filterWeightResourceOut);
    if (bHasBias)
    {
        uint32_t biasSizes[] = { 1, filterSizes[0], 1, 1 };	// One bias per output channel
        CreateWeightResource(biasSizes, biasWeightResourceOut);
    }
    else
    {
        if (biasWeightResourceOut)
            biasWeightResourceOut = nullptr;
    }

    std::vector<uint16_t> filterWeightsFP16;
    std::vector<uint16_t> biasWeightsFP16;

    const uint32_t N = filterSizes[0];
    const uint32_t C = filterSizes[1];
    const uint32_t H = filterSizes[2];
    const uint32_t W = filterSizes[3];

    std::string Output;
    std::string BiasOutput;

    for (uint32_t n = 0; n < N; n++)
    {
        switch (m_tensorLayout)
        {
        case TensorLayout::NHWC:
            // We need to convert the weights from NCHW to NHWC.
            for (uint32_t h = 0; h < H; h++)
            {
                for (uint32_t w = 0; w < W; w++)
                {
                    for (uint32_t c = 0; c < C; c++)
                    {
                        // Apply the scale weight now so we don't need a normalization layer
                        uint32_t idx = w + h * W + c * H * W + n * C * H * W;
                        //filterWeightsFP16.push_back(Float16Compressor::compress(weight));
                        filterWeightsFP16.push_back(((uint16_t*)weights.getData())[idx]);
                    }
                    Output += "\n";
                }
                Output += "\n\n";
            }
            break;

        default:
            // Weights are already in the right order
            for (uint32_t i = 0; i < C * H * W; i++)
            {
                // Apply the scale weight now so we don't need a normalization layer
                uint32_t idx = n * C * H * W + i;
                //filterWeightsFP16.push_back(Float16Compressor::compress(weight));
                filterWeightsFP16.push_back(((uint16_t*)weights.getData())[idx]);

                Output += std::to_string(Float16Compressor::decompress(((uint16_t*)weights.getData())[idx])) + " ";
                if(idx % W == (W - 1))
					Output += "\n";

                if(idx % (W * H) == (W * H - 1))
                    Output += "\n";
            }
        }

        if (bHasBias)
        {
            biasWeightsFP16.push_back(((uint16_t*)bias.getData())[n]);
            BiasOutput += std::to_string(half_to_float(((uint16_t*)bias.getData())[n])) + " ";

        }
    }

    OutputDebugString(Output.c_str());
    OutputDebugString(BiasOutput.c_str());

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

    if (bHasBias)
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

    // DirectML operators always want these resources in UAV state even if it's readonly
    D3D12_RESOURCE_BARRIER barriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(*filterWeightResourceOut, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        CD3DX12_RESOURCE_BARRIER::Transition(*biasWeightResourceOut, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    };

    commandList.ResourceBarrier(ARRAYSIZE(barriers), barriers);
}


void OpenImageDenoise::CreateWeightResource(
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
    (*d3dResourceOut)->SetName(L"WeightResource");
}
#endif