/*****************************************************************//**
 * \file   FramePipeline.hpp
 * \brief  FramePipeline,hpp .. 
 *		   一フレーム分の確定データ(FrameObject)を型別に保持し
		   パイプラインステージ間で受け渡す
 * 
 * 作成者 
 * 作成日 2026/7/18
 * 更新履歴
 * *********************************************************************/
#pragma once

#include "Defines.hpp"
#define _FRAMEPIPELINE
#ifdef _FRAMEPIPELINE

#include <atomic>
#include <cstdint>
#include <cassert>
#include <new>
#include <type_traits>

#include "RenderContext.hpp"

/// @brief 型事にユニークIDを連番で割り振る
namespace detail
{
	inline std::atomic<int> g_FrameObjectTypeCounter{ 0 };
}

template<class T>
int GetFrameObjectTypeID()
{
	static const int id = detail::g_FrameObjectTypeCounter++;
	return id;
}

class FrameAllocator
{
public:
	void Init(size_t capacity)
	{
		m_Buffer = static_cast<uint8_t*>(::operator new(capacity, std::align_val_t{ 16 }));
		m_Capacity = capacity;
		m_Offset = 0;
	}

	~FrameAllocator()
	{
		if (m_Buffer)
		{
			::operator delete(m_Buffer, std::align_val_t{ 16 });
		}
	}

	void* Allocate(size_t size, size_t align = 16)
	{
		const size_t aligned = (size + align - 1) & ~(align - 1);
		const size_t old = m_Offset.fetch_add(aligned, std::memory_order_relaxed);
		assert(old + aligned <= m_Capacity && "FrameAllocator容量不足: Initのサイズを増やす");
		return m_Buffer + old;
	}
	void Reset()
	{
		m_Offset.store(0, std::memory_order_relaxed);
	}

private:
	uint8_t* m_Buffer = nullptr;
	size_t m_Capacity = 0;
	std::atomic<size_t> m_Offset{ 0 };
};

class FramePipeline
{
	static constexpr int MAX_TYPES = 128;

	struct Node
	{
		Node* next;
		void (*destructor)(void*);
	};
public:
	void Init(size_t allocatorSize = 1 * 1024 * 1024)
	{
		m_Allocator.Init(allocatorSize);
	}

	void Reset(UINT64 frameNumber)
	{
		for (auto& head : m_Heads)
		{
			for (Node* n = head.load(std::memory_order_relaxed); n; n = n->next)
			{
				if (n->destructor)
				{
					n->destructor(reinterpret_cast<uint8_t*>(n) + sizeof(Node));
				}
			}
			head.store(nullptr, std::memory_order_relaxed);
		}

		for (auto& fixed : m_Fixed)
		{
			fixed.store(false,std::memory_order_relaxed);
		}
		m_Allocator.Reset();
		m_FrameNumber = frameNumber;
	}

	UINT64 GetFrameNumber() const noexcept
	{
		return m_FrameNumber;
	}

	template<class T,class... Args>
	T* AddFrameObject(Args&&... args)
	{
		const int id = GetFrameObjectTypeID<T>();
		assert(id < MAX_TYPES);
		assert(!m_Fixed[id].load(std::memory_order_acquire)
			&& "Fix済みのFrameObject型に追加しようとした(登録タイミングが遅い)");

		void* mem = m_Allocator.Allocate(sizeof(Node) + sizeof(T),
			(std::max)(alignof(Node), alignof(T)));
		Node* node = static_cast<Node*>(mem);
		T* obj = new (static_cast<uint8_t*>(mem) + sizeof(Node)) T(std::forward<Args>(args)...);

		node->destructor = std::is_trivially_destructible_v<T>
			? nullptr
			: +[](void* p) { static_cast<T*>(p)->~T(); };

		// ロックフリーpush(追加のみ・削除なしなのでABA問題なし)
		Node* head = m_Heads[id].load(std::memory_order_relaxed);
		do
		{
			node->next = head;
		} while (!m_Heads[id].compare_exchange_weak(head, node,
			std::memory_order_release, std::memory_order_relaxed));

		return obj;
	}

	// ---- 単体取得: 最後に登録されたものを返す ----
	template<class T>
	const T* GetFrameObject() const
	{
		Node* head = m_Heads[GetFrameObjectTypeID<T>()].load(std::memory_order_acquire);
		if (head == nullptr)
		{
			return nullptr;
		}
		return reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(head) + sizeof(Node));
	}

	// ---- リスト走査(取得時点のスナップショット) ----
	template<class T, class Fn>
	void ForEachFrameObject(Fn&& fn) const
	{
		for (Node* n = m_Heads[GetFrameObjectTypeID<T>()].load(std::memory_order_acquire);
			n; n = n->next)
		{
			fn(*reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(n) + sizeof(Node)));
		}
	}

	// ---- 消費タイミングで追加禁止に(登録漏れ・遅延登録の検出) ----
	template<class T>
	void FixFrameObject()
	{
		m_Fixed[GetFrameObjectTypeID<T>()].store(true, std::memory_order_release);
	}

	/// @brief FrameObject に添える可変長データ用のメモリを借りる
	/// @note 生存期間は次の Reset まで。デストラクタは走らないので
	///       trivially destructible なものだけを置くこと
	void* AllocateFrameMemory(size_t size, size_t align = 16)
	{
		return m_Allocator.Allocate(size, align);
	}

private:
	UINT64 m_FrameNumber = 0;
	FrameAllocator m_Allocator;
	std::atomic<Node*> m_Heads[MAX_TYPES]{};
	std::atomic<bool> m_Fixed[MAX_TYPES]{};
};

namespace detail
{
	inline thread_local FramePipeline* tls_FramePipeline = nullptr;
}

inline FramePipeline* GetFrameThreadPipeline()
{
	assert(detail::tls_FramePipeline && "FramePipelineスコープ外からの取得");
	return detail::tls_FramePipeline;
}

inline FramePipeline* GetFrameThreadPipelineNullable()
{
	return detail::tls_FramePipeline;
}

class FramePipelineScope
{
public:
	explicit FramePipelineScope(FramePipeline* p) noexcept
		: m_Prev(detail::tls_FramePipeline)
	{
		detail::tls_FramePipeline = p;
	}
	~FramePipelineScope() { detail::tls_FramePipeline = m_Prev; }
	FramePipelineScope(const FramePipelineScope&) = delete;
	FramePipelineScope& operator=(const FramePipelineScope&) = delete;
private:
	FramePipeline* m_Prev;
};

// そのフレームで確定した描画設定のスナップショット
struct FO_RenderSettings
{
	E_VERTEX_SHADER vertexShader;
	E_PIXEL_SHADER  pixelShader;
	bool wireframe;
	bool meshShader;
};

/// @brief カメラの用途
/// @note CameraComponent::CameraType と同じ並び。
///       Components.hpp への依存を持ち込まないためにここで定義し直している
enum class FO_CameraType : int
{
	Main,
	Secondary
};

// そのフレームで確定したカメラ(存在するぶんだけ登録される)
struct FO_Camera
{
	FO_CameraType type;
	float4x4 view;
	float4x4 projection;
	float3   position;

	// 被写界深度などが射影の near/far を要求するので持たせる
	float nearZ = 0.1f;
	float farZ = 100.0f;
};

// そのフレームで確定したライト定数(LightSystem の出力スナップショット)
struct FO_Light
{
	LightCB cb;
};

#endif
