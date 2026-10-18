/*****************************************************************//**
 * \file   RHITypes.hpp
 * \brief  RHI 共通フォーマットの定義
 * 
 * 作成者 keepmm
 * 作成日 2026/9/29
 * 更新履歴 9.29 作成
 * memo RHI(Render Hardware Interface
 * *********************************************************************/
#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

// 前方宣言

class IRHIDevice;
class IRHITexture;
class IRHIBuffer;
class IRHIPipelineLayout;
class IRHIPipelineState;
class IRHIDescriptorTable;
class IRHICommandList;
class IRHIFence;
class IRHISwapChain;

// GPUオブジェクトの参照

using RHITextureRef = std::shared_ptr<IRHITexture>;
using RHIBufferRef = std::shared_ptr<IRHIBuffer>;
using RHIPipelineLayoutRef = std::shared_ptr<IRHIPipelineLayout>;
using RHIPipelineStateRef = std::shared_ptr<IRHIPipelineState>;
using RHIDescriptorTableRef = std::shared_ptr <IRHIDescriptorTable>;
using RHICommandListRef = std::shared_ptr<IRHICommandList>;
using RHIFenceRef = std::shared_ptr<IRHIFence>;
using RHISwapChainRef = std::shared_ptr<IRHISwapChain>;

/// @brief RHI オブジェクトの共通基底
class IRHIObject
{
public:
	virtual ~IRHIObject() = default;

	IRHIObject(const IRHIObject&) = delete;
	IRHIObject& operator=(const IRHIObject&) = delete;
protected:
	IRHIObject() = default;
};

// ビットフラグ eum の演算子
#define RHI_ENUM_FLAGS(E)														\
	constexpr E operator|(E a, E b) noexcept									\
	{																			\
		using U = std::underlying_type_t<E>;									\
		return static_cast<E>(static_cast<U>(a) | static_cast<U>(b));			\
	}																			\
	constexpr E operator&(E a, E b) noexcept									\
	{																			\
		using U = std::underlying_type_t<E>;									\
		return static_cast<E>(static_cast<U>(a) & static_cast<U>(b));			\
	}																			\
	constexpr E& operator|=(E& a, E b) noexcept { return a = a | b; }			\
	constexpr bool HasAnyFlag(E v, E f) noexcept								\
	{																			\
		using U = std::underlying_type_t<E>;									\
		return (static_cast<U>(v) & static_cast<U>(f)) != 0;					\
	}															  \

// バックエンド
enum class ERHIBackend : uint8_t
{
	D3D11,
	D3D12,
	Vulkan,
	// Metal
};

// フォーマット
enum class ERHIFormat : uint8_t
{
	Unknown = 0,

	// 8bit/ch
	R8G8B8A8_UNORM,
	R8G8B8A8_UNORM_SRGB,
	B8G8R8A8_UNORM,			// Vulkan のスワップチェインはこちらしか出ない環境がある
	B8G8R8A8_UNORM_SRGB,

	// パック
	R10G10B10A2_UNORM,
	R11G11B10_FLOAT,

	// 16bit
	R16_UINT,	// インデックス
	R16G16B16A16_FLOAT,	// HDRシーン

	// 32bit
	R32_UINT,	// インデックス
	R32_FLOAT,
	R32G32_FLOAT,
	R32G32B32_FLOAT,	// 頂点属性用
	R32G32B32A32_FLOAT,
	R32G32B32A32_UINT,	// BLENDINDICES
	
	// 深度
	D32_FLOAT,

	// ブロック圧縮(DxTex で DDX を読む場合)
	BC1_UNORM,
	BC1_UNORM_SRGB,
	BC3_UNORM,
	BC3_UNORM_SRGB,
	BC5_UNORM,
	BC7_UNORM,
	BC7_UNORM_SRGB,

	Count
};

constexpr bool IsDepthFormat(ERHIFormat f) noexcept
{
	return f == ERHIFormat::D32_FLOAT;
}

constexpr bool IsCompressedFormat(ERHIFormat f) noexcept
{
	return f >= ERHIFormat::BC1_UNORM && f <= ERHIFormat::BC7_UNORM_SRGB;
}

// 1 要素当たりのバイト数 (BC刑は 4x4 ブロック当たり)
constexpr uint32_t GetFormatByteSize(ERHIFormat f)noexcept
{
	switch (f)
	{
	case ERHIFormat::R16_UINT: return 2;
	case ERHIFormat::R8G8B8A8_UNORM:
	case ERHIFormat::R8G8B8A8_UNORM_SRGB:
	case ERHIFormat::B8G8R8A8_UNORM:
	case ERHIFormat::B8G8R8A8_UNORM_SRGB:
	case ERHIFormat::R10G10B10A2_UNORM:
	case ERHIFormat::R11G11B10_FLOAT:
	case ERHIFormat::R32_UINT:
	case ERHIFormat::R32_FLOAT:
	case ERHIFormat::D32_FLOAT: return 4;
	case ERHIFormat::R16G16B16A16_FLOAT:
	case ERHIFormat::R32G32_FLOAT: return 8;
	case ERHIFormat::R32G32B32_FLOAT: return 12;
	case ERHIFormat::R32G32B32A32_FLOAT:
	case ERHIFormat::R32G32B32A32_UINT: return 16;
	case ERHIFormat::BC1_UNORM:
	case ERHIFormat::BC1_UNORM_SRGB: return 8;
	case ERHIFormat::BC3_UNORM:
	case ERHIFormat::BC3_UNORM_SRGB:
	case ERHIFormat::BC5_UNORM:
	case ERHIFormat::BC7_UNORM:
	case ERHIFormat::BC7_UNORM_SRGB: return 16;
	default: return 0;
	}
}

// リソース状態

enum class ERHIResourceState : uint32_t
{
	Undefined = 0,	// 中身不要 D3D12 : COMMON / VK : LAYOUT_UNDEFINED(内容破棄) 
	Common = 1u << 0,	// D3D12 : COMMON / VK : GENERAL
	VertexBuffer = 1u << 1, // D3D12 : VERTEX_AND_CONSTANT_BUFFER / VK : VERTEX_BUFFER
	IndexBuffer = 1u << 2,	// D3D12 : INDEX
	ConstantBuffer = 1u << 3, // D3D12 : VERTEX_AND_CONSTANT_BUFFER / VK : UNIFORM_BUFFER
	ShaderResourcePS = 1u << 4, // D3D12 : PIXEL_SHADER_RESOURCE
	ShaderResourceNonPS = 1u << 5, // D3D12 NON_PIXEL_SHADER_RESOURCE(VSのボーン SRV等)
	UnorderedAccess = 1u << 6,
	RenderTarget = 1u << 7,
	DepthWrite = 1u << 8,
	DepthRead = 1u << 9,
	CopySrc = 1u << 10,
	CopyDst = 1u << 11,
	IndirectArgument = 1u << 12,
	Present = 1u << 13,

	ShaderResource = ShaderResourcePS | ShaderResourceNonPS,

	// Uploadバッファの固定状態 D3D12_RESOURCE_STATE_GENERIC_READと同じ
	GenericRead = VertexBuffer | IndexBuffer | ConstantBuffer | ShaderResource | IndirectArgument | CopySrc,
};
RHI_ENUM_FLAGS(ERHIResourceState)

// 書き込み状態かどうか
constexpr bool IsWriteState(ERHIResourceState s)noexcept
{
	return HasAnyFlag(s, ERHIResourceState::UnorderedAccess | ERHIResourceState::RenderTarget | ERHIResourceState::DepthWrite | ERHIResourceState::CopyDst);
}

/// @brief PSOに持たせる Vulkan はトポロジーがパイプラインに焼き込まれる
enum class ERHIPrimitiveTopology : uint8_t
{
	TriangleList,
	TriangleStrip,
	LineList,
	LineStrip,
	PointList
};

/// @brief メモリ / 用途 / 次元
enum class ERHIMemoryType : uint8_t
{
	GpuOnly,	// D3D12 : DEFAULT / VK : DEVICE_LOCAL
	Upload,		// D3D12 : UPLOAD  / VK : HOST_VISIBLE | HOST_COHERENT(CPU->GPU) 
	ReadBack	// D3D12 : READBACK / VK : HOST_VISIBLE | HOST_COHERENT(GPU->CPU)
};

enum class ERHITextureUsage : uint8_t
{
	None = 0,
	ShaderResource = 1u << 0,
	RenderTarget = 1u << 1,
	DepthStencil = 1u << 2,
	UnorderedAccess = 1u << 3,
};
RHI_ENUM_FLAGS(ERHITextureUsage)

/// @brief バッファ用途 D3D12はほぼ不要 VulkanにはVkBufferUsageFlags に必須
enum class ERHIBufferUsage : uint32_t
{
	None = 0,
	VertexBuffer = 1u << 0,
	IndexBuffer = 1u << 1,
	ConstantBuffer = 1u << 2,
	ShaderResource = 1u << 3,
	UnorderedAccess = 1u << 4,
	IndirectArgs = 1u << 5,
	CopySrc = 1u << 6,
	CopyDst = 1u << 7,
};
RHI_ENUM_FLAGS(ERHIBufferUsage)

enum class ERHITextureDimension : uint8_t
{
	Texture2D,
	Texture2DArray,
	TextureCube,
	Texture3D,
};


/// @brief 最適化クリア値
/// @brief aaa
struct RHIClearValue
{
	float color[4] = { 0.0f,0.0f,0.0f,1.0f };
	float depth = 1.0f;
	uint8_t stencil = 0;
};

/// @brief テクスチャ生成情報 生成直後の状態は常にERHIResouceState::Undeifned
struct RHITextureDesc
{
	ERHITextureDimension dimension = ERHITextureDimension::Texture2D;
	uint32_t width				= 1;
	uint32_t height				= 1;
	uint32_t depthOrArraySize	= 1;	// Cube は6
	uint32_t mipLevels			= 1;
	uint32_t sampleCount		= 1;
	ERHIFormat format = ERHIFormat::R8G8B8A8_UNORM;
	ERHITextureUsage usage = ERHITextureUsage::ShaderResource;
	RHIClearValue clearValue{};
	const char* debugName = nullptr;
};

/// @brief バッファ生成情報 初期状態はmemoryで決める
/// @brief GpuOnly = Undefined / Upload = generiRead (固定) ReadBack = COpyDst(固定)
struct RHIBufferDesc
{
	uint64_t size = 0;
	uint32_t stride = 0;	// structuredBufferの要素サイズ それ以外は0

	ERHIBufferUsage usage = ERHIBufferUsage::None;
	ERHIMemoryType memory = ERHIMemoryType::GpuOnly;
	const char* debugName = nullptr;
};

/// @brief テクスチャのサブリソース 1 つぶんの、バッファ上での並び（コピー用）
/// @note  行の間隔の決まりは API ごとに違う（D3D12 は 256 バイト境界）ので、
///        自分で計算せず IRHIDevice::GetCopyableFootprints に求めさせる
struct RHISubresourceFootprint
{
	uint64_t offset = 0;	// バッファ先頭からの位置
	uint32_t rowPitch = 0;	// 1 行の間隔（バイト）。BC 形式は 4x4 ブロック 1 列が 1 行
	uint32_t rowSize = 0;	// 1 行の実データのバイト数（rowPitch 以下）
	uint32_t rowCount = 0;	// 行数（BC 形式はブロックの行数）
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t depth = 1;
};

/// @brief CPU 側の画像データ 1 サブリソースぶん（DirectX::Image と同じ並び）
struct RHISubresourceData
{
	const void* data = nullptr;
	uint64_t rowPitch = 0;		// 1 行のバイト数
	uint64_t slicePitch = 0;	// 1 枚（3D は 1 スライス）のバイト数
};

/// @brief サブリソース番号 = mip + 配列番号 × mip 数（D3D12 と同じ数え方。Cube は配列 6 枚）
constexpr uint32_t RHISubresourceIndex(uint32_t mip, uint32_t arraySlice, uint32_t mipLevels) noexcept
{
	return mip + arraySlice * mipLevels;
}

/// @brief パイプラインレイアウト
enum class ERHIShaderStage : uint8_t
{
	None = 0,
	Vertex = 1u << 0,
	Pixel = 1u << 1,
	All = Vertex | Pixel,
};
RHI_ENUM_FLAGS(ERHIShaderStage)

enum class ERHICompareFunc : uint8_t
{
	Never,
	Less,
	Equal,
	LessEqual,
	Greater,
	NotEqual,
	GreaterEqual,
	Always,
};

enum class ERHIFilter : uint8_t
{
	Point,
	Linear,
	Anisotropic,
};

enum class ERHIAddressMode : uint8_t
{
	Wrap,
	Mirror,
	Clamp,
	Border,
};

/// @brief Vulkanは拡張なしだと固定 3 色のみ (D3D12のstatic samplerと同じ制約)
enum class ERHIBorderColor : uint8_t
{
	TransparentBlack,
	OpaqueBlack,
	OpaqueWhite,
};

/// @brief 静的サンプラー
/// @brief D3D12 : static sampler 
/// @brief VK : immutable sampler 
struct ERHIStaticSamplerDesc
{
	uint32_t shaderRegister = 0;
	uint32_t registerSpace = 0;
	ERHIFilter filter = ERHIFilter::Linear;	// min/mag/mip 共通
	ERHIAddressMode address = ERHIAddressMode::Wrap;	// U/V/W 共通
	bool compareEnable = false;	// シャドウ用比較サンプラー
	ERHICompareFunc compareFunc = ERHICompareFunc::LessEqual;
	ERHIBorderColor borderColor = ERHIBorderColor::OpaqueWhite;
	uint32_t maxAnisotropy = 1;
	ERHIShaderStage visibility = ERHIShaderStage::All;
};

enum class ERHIBindingType : uint8_t
{
	ConstantBuffer,
	StructuredBuffer,
	TextureTable,
};

/// @brief レイアウトの 1 スロット (=D3D12 のルートパラメータ一個分)
struct RHIBindingSlot
{
	ERHIBindingType type = ERHIBindingType::ConstantBuffer;
	uint32_t shaderRegister = 0;
	uint32_t registerSpace = 0;// HLSLの b# / t#の番号をそのまま描く
	uint32_t count = 1;	// StructuredBuffer / TextureTable の配列数
	ERHIShaderStage visibility = ERHIShaderStage::All;
};

/// @brief ルートシグネチャー / VkPipelineLayout 相当
struct RHIPipelineLayoutDesc
{
	std::vector<RHIBindingSlot> slots;	// 添え字 = コマンドリストで SetするSlot番号
	std::vector<ERHIStaticSamplerDesc> staticSamplers;
	const char* debugName = nullptr;
};

/// @brief コンパイル済みシェーダー
/// @brief D3D12 : DXBC/DXIL
/// @brief Vulkan : SPIR-V(DXC -spirv)
/// @brief HLSL -> バイト列のコンパイルは ShaderLibrary の仕事でRHIはバイト列を受け取るだけ
struct RHIShaderByteCode
{
	const void* data = nullptr;
	size_t size = 0;
	const char* entrypoint = nullptr;

	bool IsValid() const noexcept
	{
		return data != nullptr && size != 0;
	}
};

/// @brief 頂点属性 1 個
struct RHIVertexAttribute
{
	const char* semanticName = nullptr; //DX用
	uint32_t semanticIndex = 0;	//DX用
	ERHIFormat format = ERHIFormat::Unknown;
	uint32_t bufferSlot = 0;
	uint32_t offset = 0;
};

/// @brief 頂点バッファ 1 本ぶんのレイアウト]
/// @brief vulkanはストライドを PSO で要求するのでここで待つ
struct RHIVertexBufferLayout
{
	uint32_t stride = 0;
	bool perInstance = false;
};

enum class ERHIFillMode : uint8_t
{
	Solid,
	Wireframe,
};

enum class ERHICullMode : uint8_t
{
	None,
	Front,
	Back,
};

struct RHIRasterizerDesc
{
	ERHIFillMode fillMode = ERHIFillMode::Solid;
	ERHICullMode cullMode = ERHICullMode::Back;

	bool frontCounterClockwise = false;
	int32_t depthBias = 0;
	float depthBiasClamp = 0.0f;
	float slopeScaledDepthBias = 0.0f;
	bool depthClipEnable = true;	// falseは VK : depthClamp機能が必要
};

struct RHIDepthStencilDesc
{
	bool depthTestEnable = true;
	bool depthWriteEnable = true;
	// CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT)同等
	ERHICompareFunc depthFunc = ERHICompareFunc::Less;
};

enum class ERHIBlendFactor : uint8_t
{
	Zero,
	One,
	SrcColor,
	InvSrcColor,
	SrcAlpha,
	InvSrcAlpha,
	DstColor,
	InvDstColor,
	DstAlpha,
	InvDstAlpha,
};

enum class ERHIBlendOp : uint8_t
{
	Add,
	Subtract,
	RevSubtract,
	Min,
	Max,
};

enum class ERHIColorWriteMask : uint8_t
{
	None = 0,
	R = 1u << 0,
	G = 1u << 1,
	B = 1u << 2,
	A = 1u << 3,
	All = R | G | B | A,
};
RHI_ENUM_FLAGS(ERHIColorWriteMask)

struct RHIBlendDesc
{
	bool blendEnable = false;
	ERHIBlendFactor srcColor = ERHIBlendFactor::One;
	ERHIBlendFactor dstColor = ERHIBlendFactor::Zero;
	ERHIBlendOp colorOp = ERHIBlendOp::Add;
	ERHIBlendFactor srcAlpha = ERHIBlendFactor::One;
	ERHIBlendFactor dstAlpha = ERHIBlendFactor::Zero;
	ERHIBlendOp alphaOp = ERHIBlendOp::Add;
	ERHIColorWriteMask writeMask = ERHIColorWriteMask::All;

	/// @brief 半透明(SrcAlpha + InvSrcAlpha) 透過合成
	static RHIBlendDesc AlphaBlend() noexcept
	{
		RHIBlendDesc d;
		d.blendEnable = true;
		d.srcColor = ERHIBlendFactor::SrcAlpha;
		d.dstColor = ERHIBlendFactor::InvSrcAlpha;
		return d;
	}

	/// @brief 加算 (ビーム/ボリューム/ブルーム合成)
	static RHIBlendDesc Additive() noexcept
	{
		RHIBlendDesc d;
		d.blendEnable = true;
		d.srcColor = ERHIBlendFactor::One;
		d.dstColor = ERHIBlendFactor::One;
		return d;
	}
};

static constexpr uint32_t RHI_MAX_RENDER_TARGETS = 8;

/// @brief グラフィックスPSo生成情報
struct RHIGraphicsPipelineDesc
{
	RHIPipelineLayoutRef layout;	// PSO が持つので、作った後に手放してよい

	RHIShaderByteCode vs;
	RHIShaderByteCode ps;

	std::vector<RHIVertexAttribute> vertexAttributes;
	std::vector<RHIVertexBufferLayout> vertexBuffers;
	ERHIPrimitiveTopology topology = ERHIPrimitiveTopology::TriangleList;

	RHIRasterizerDesc rasterizer{};
	RHIDepthStencilDesc depthStencil{};
	RHIBlendDesc blend[RHI_MAX_RENDER_TARGETS]{};

	uint32_t numRenderTargets = 1;
	ERHIFormat rtvFormat[RHI_MAX_RENDER_TARGETS]{ ERHIFormat::R8G8B8A8_UNORM };
	ERHIFormat dsvFormat = ERHIFormat::D32_FLOAT;	//深度なしはUnknown
	uint32_t sampleCount = 1;
	const char* debugName = nullptr;
};

/// @brief D3D12と同じ左上原点 y 下向きで指定する
/// @brief Vulkan バックエンドは { y + height, -height } に変換して渡す(負の高さ。VK 1.1で標準)
struct RHIViewport
{
	float x = 0.0f;
	float y = 0.0f;
	float width = 0.0f;
	float height = 0.0f;
	float minDepth = 0.0f;
	float maxDepth = 1.0f;
};

struct RHIRect
{
	int32_t left = 0;
	int32_t top = 0;
	int32_t right = 0;
	int32_t bottom = 0;
};

struct RHIDeviceDesc
{
	ERHIBackend backend = ERHIBackend::D3D12;
	uint32_t frameInFlight = 0;
	bool enableDebugLayer = false;	// D3D12 : DebugLayer / Vulkan : VK_LAYER_KHRONOS_validation
	bool enableGpuValidation = false;
};

struct RHIDeviceCaps
{
	std::string adapterName;
	bool meshShader = false;
	uint32_t constantBufferAlignment = 256; // DX12は 256固定
	uint32_t maxTextureSize2D = 0;
};

struct RHISwapChainDesc
{
	void* windowHandle = nullptr;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t bufferCount = 0;

	/// @brief 希望フォーマット。通さない環境では近いものが選ばれるので
	ERHIFormat format = ERHIFormat::R8G8B8A8_UNORM;
	bool vSync = true;
};
