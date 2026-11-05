/*****************************************************************//**
 * \file   ComponentType.hpp
 * \brief  コンポーネントの型番号と、肩を消して扱うための情報
 * 
 * \author keepm
 * \date   October 2026
 *********************************************************************/
#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace TE
{
	using ComponentTypeID = std::uint32_t;

	struct ComponentInfo
	{
		std::size_t size = 0;
		std::size_t alignment = 0;
		bool enableable = false;
		void (*moveConstruct)(void* destination, void* source) = nullptr;
		void (*destroy)(void* object) = nullptr;
		void (*defaultConstruct)(void* destination) = nullptr;
		void (*copyConstruct)(void* destionation, const void* source) = nullptr;
	};

	template<typename T>
	inline constexpr bool kIsEnableable = [] {
		if constexpr (requires {T::kEnableable; }) {
			return static_cast<bool>(T::kEnableable);
		}
		else {
			return false;
		}}();

		namespace detail
		{
			inline std::vector<ComponentInfo>& ComponentInfos() {
				static std::vector<ComponentInfo> infos;
				return infos;
			}

			template <typename T>
			ComponentTypeID RegisterComponent()
			{
				// 引っ越し途中で例外が出ると、壊れた状態になる
				static_assert(std::is_nothrow_move_constructible_v<T>, "コンポーネントは例外を出さずに移動できる型にする");
				static_assert(std::is_nothrow_destructible_v<T>, "コンポーネントのデストラクタは例外を出さないこと");

				ComponentInfo info;
				info.size = sizeof(T);
				info.alignment = alignof(T);
				info.enableable = kIsEnableable<T>;
				info.moveConstruct = [](void* destination, void* source)
					{
						::new(destination) T(std::move(*static_cast<T*>(source)));
					};
				info.destroy = [](void* object)
					{
						static_cast<T*>(object)->~T();
					};
				// 既定値で作るのは引っ越しの後（チャンクの中）なので、例外を出さない型だけ許す
				if constexpr (std::is_nothrow_default_constructible_v<T>)
				{
					info.defaultConstruct = [](void* destination)
						{
							::new(destination) T();
						};
				}
				if constexpr (std::is_copy_constructible_v<T>)
				{
					info.copyConstruct = [](void* destination, const void* source)
						{
							::new(destination) T(*static_cast<const T*>(source));
						};
				}

				auto& infos = ComponentInfos();
				infos.push_back(info);
				return static_cast<ComponentTypeID>(infos.size() - 1);
			}
		}

		template<typename T>
		ComponentTypeID ComponentTypeIDOf()
		{
			static_assert(std::is_same_v<T, std::remove_cvref_t<T>>, "コンポーネントの型にconstや参照を付けない");
			static const ComponentTypeID id = detail::RegisterComponent<T>();
			return id;
		}

		inline const ComponentInfo& GetComponentInfo(ComponentTypeID id)
		{
			return detail::ComponentInfos()[id];
		}
}
