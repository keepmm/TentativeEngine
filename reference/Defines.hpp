/*****************************************************************//**
 * \file   Defines.hpp
 * \brief  定義ファイル
 * 
 * 作成者 
 * 作成日 2026/2/17
 * 更新履歴
 * *********************************************************************/
#pragma once

#pragma once

#define NOMINMAX
#define _FRAMEPIPELINE

 // DirectX 12 
#include <windows.h>
#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <DirectXColors.h>
#include <DirectXCollision.h>
#include <d3dcompiler.h>

// C++標準ライブラリ
#include <memory>
#include <vector>
#include <string>
#include <stdexcept>
#include <array>
#include <chrono>
#include <unordered_map>

// FILE I/O
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cassert>
#include <ostream>
#include <unordered_set>
#include <type_traits>
#include <utility>

// 非同期処理
#include <thread>
#include <queue>
#include <mutex>
#include <functional>
#include <condition_variable>
#include <future>


 // -------------------------------//
 //			 型エイリアス			   //
 // -------------------------------//
using float3 = DirectX::XMFLOAT3;
using float4 = DirectX::XMFLOAT4;
using float2 = DirectX::XMFLOAT2;
using matrix = DirectX::XMMATRIX;
using vector = DirectX::XMVECTOR;
using float4x4 = DirectX::XMFLOAT4X4;

using COLOR = DirectX::XMFLOAT4;
using POSITION = DirectX::XMFLOAT3;
using UV = DirectX::XMFLOAT2;
using QUATERNION = DirectX::XMFLOAT4;
using SCALE = DirectX::XMFLOAT3;

// -------------------------------//
//		  演算子オーバーロード		  //
// -------------------------------//

// float3 + float3 の演算子オーバーロード
inline float3 operator+(const float3& a, const float3& b)
{
	return float3(a.x + b.x, a.y + b.y, a.z + b.z);
}

inline float3 operator-(const float3& a, const float3& b)
{
	return float3(a.x - b.x, a.y - b.y, a.z - b.z);
}

inline float3 operator*(const float3& a, const float3& b)
{
	return float3(a.x * b.x, a.y * b.y, a.z * b.z);
}

inline float3 operator*(const float3& a,float b)
{
	return float3(a.x * b, a.y * b, a.z *b);
}

inline float3 operator-=(float3& a, const float3& b)
{
	a.x -= b.x;
	a.y -= b.y;
	a.z -= b.z;
	return a;
}

inline float3 operator+=(float3& a, const float3& b)
{
	a.x += b.x;
	a.y += b.y;
	a.z += b.z;
	return a;
}

inline float3 operator*=(float3& a, const float3& b)
{
	a.x *= b.x;
	a.y *= b.y;
	a.z *= b.z;
	return a;
}


inline vector operator+(const vector& a, const vector& b)
{
	return DirectX::XMVectorAdd(a, b);
}

inline vector operator-(const vector& a, const vector& b)
{
	return DirectX::XMVectorSubtract(a, b);
}

inline vector operator*(const vector& a, const vector& b)
{
	return DirectX::XMVectorMultiply
	(a, b);
}

inline vector operator*(const vector& a, const float& b)
{
	return DirectX::XMVectorScale(a, b);
}

inline vector operator/(const vector& a, const float& b)
{
	return DirectX::XMVectorScale(a, 1.0f / b);
}

inline vector& operator+=(vector& a, const vector& b)
{
	a = DirectX::XMVectorAdd(a, b);
	return a;
}

#include <tchar.h>
#define CLASS_NAME _T("DX12Test")
#define PROC_NAME _T("DX12Test")

#define WINDOW_WIDTH 1920
#define WINDOW_HEIGHT 1080

#define FPS 60
#define FRAME_TIME (1000 / FPS)

#include <wrl/client.h>
template<typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

template<typename T, typename... Args>
std::unique_ptr<T> MakeUnique(Args&&... args)
{
    return std::make_unique<T>(std::forward<Args>(args)...);
}


template<typename T, typename... Args>
std::shared_ptr<T> MakeShared(Args&&... args)
{
	return std::make_shared<T>(std::forward<Args>(args)...);
}

/// @brief バッファの数
static constexpr int RTV_NUM = 3;
