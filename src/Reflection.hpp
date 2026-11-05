/*****************************************************************//**
 * \file   Reflection.hpp
 * \brief  コンポーネントの型の名前とフィールドの一覧（ECS の外に置く）
 *
 * \author keepm
 * \date   November 2026
 *********************************************************************/
#pragma once

#include "Core/FixedString.hpp"
#include "ECS/ComponentType.hpp"
#include "Math/Color.hpp"
#include "Math/Quaternion.hpp"
#include "Math/Vector2.hpp"
#include "Math/Vector3.hpp"
#include "Math/Vector4.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

namespace TE
{
	// Inspectorと保持で扱える型
	enum class FieldType : std::uint8_t
	{
		Bool,
		Int32,
		UInt32,
		Float,
		Vector2,
		Vector3,
		Vector4,
		Quaternion,	// UI ではオイラー角で見せる
		Color,
		String,		// FixedString<N>。size が容量
	};

	enum class FieldFlags : std::uint8_t
	{
		None = 0,
		ReadOnly = 1 << 0,	// 表示だけ（計算結果や番号など）
	};

	constexpr FieldFlags operator|(FieldFlags a, FieldFlags b)
	{
		return static_cast<FieldFlags>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
	}
	constexpr bool HasFlag(FieldFlags flags, FieldFlags flag)
	{
		return (static_cast<std::uint8_t>(flags) & static_cast<std::uint8_t>(flag)) != 0;
	}

	struct FieldInfo
	{
		const char* name;
		FieldType type;
		std::uint64_t offset;	// offsetof。メンバーポインタではなくオフセットにして、型を消したまま読み書きする
		std::uint32_t size;
		FieldFlags flags = FieldFlags::None;
	};

	struct TypeInfo
	{
		const char* name = nullptr;
		std::vector<FieldInfo> fields;
	};

	// ---- C++ の型 → FieldType ---- //
	// 扱えない型を TE_FIELD に書くと、ここで「未定義」のコンパイルエラーになる

	template <typename T> struct FieldTypeOf;
	template <> struct FieldTypeOf<bool> { static constexpr FieldType kValue = FieldType::Bool; };
	template <> struct FieldTypeOf<std::int32_t> { static constexpr FieldType kValue = FieldType::Int32; };
	template <> struct FieldTypeOf<std::uint32_t> { static constexpr FieldType kValue = FieldType::UInt32; };
	template <> struct FieldTypeOf<float> { static constexpr FieldType kValue = FieldType::Float; };
	template <> struct FieldTypeOf<Vector2> { static constexpr FieldType kValue = FieldType::Vector2; };
	template <> struct FieldTypeOf<Vector3> { static constexpr FieldType kValue = FieldType::Vector3; };
	template <> struct FieldTypeOf<Vector4> { static constexpr FieldType kValue = FieldType::Vector4; };
	template <> struct FieldTypeOf<Quaternion> { static constexpr FieldType kValue = FieldType::Quaternion; };
	template <> struct FieldTypeOf<Color> { static constexpr FieldType kValue = FieldType::Color; };
	template <std::size_t N> struct FieldTypeOf<FixedString<N>> { static constexpr FieldType kValue = FieldType::String; };

	template <typename Member>
	FieldInfo MakeField(const char* name, std::size_t offset, FieldFlags flags = FieldFlags::None)
	{
		return { name, FieldTypeOf<Member>::kValue, offset, static_cast<std::uint32_t>(sizeof(Member)), flags };
	}

	class TypeRegistry
	{
	public:
		/// @brief T の名前とフィールドを登録する。ComponentTypeIDOf<T>() の番号もここで決まる
		template <typename T>
		void Register(const char* name, std::initializer_list<FieldInfo> fields)
		{
			const ComponentTypeID id = ComponentTypeIDOf<T>();
			if (id >= m_Types.size())
			{
				m_Types.resize(id + 1);
			}
			assert(m_Types[id].name == nullptr && "同じ型を 2 回登録している");
			m_Types[id] = TypeInfo{ name, fields };
			m_Order.push_back(id);
		}

		// 登録されていない型は nullptr（Inspector では名前だけ「(not reflected)」で出す）
		const TypeInfo* Find(ComponentTypeID id) const
		{
			return id < m_Types.size() && m_Types[id].name != nullptr ? &m_Types[id] : nullptr;
		}

		// Add Component のメニュー用。登録順
		std::span<const ComponentTypeID> RegisteredTypes() const { return m_Order; }

	private:
		std::vector<TypeInfo> m_Types;          // 添字 = ComponentTypeID（無ければ name == nullptr）
		std::vector<ComponentTypeID> m_Order;
	};
}

// フィールド 1 つぶんを書くマクロ。型（FieldType）は decltype から決める。
// フラグ付きは TE_FIELD_EX（空の __VA_ARGS__ の前のカンマの扱いが MSVC の既定のプリプロセッサと標準で違うため、分けておく）
#define TE_FIELD(Type, member) \
	::TE::MakeField<decltype(Type::member)>(#member, offsetof(Type, member))
#define TE_FIELD_EX(Type, member, flags) \
	::TE::MakeField<decltype(Type::member)>(#member, offsetof(Type, member), flags)
