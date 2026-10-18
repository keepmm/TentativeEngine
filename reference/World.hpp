/*****************************************************************//**
 * \file   World.hpp
 * \brief  Scene内のEntityとComponentを管理するクラス
 * 
 * 作成者 keeeep
 * 作成日 2026/2/16
 * 更新履歴	2.16 作成
 *			5.25 Entity削除処理を追加
 * *********************************************************************/
#pragma once

#include <cstdint>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>
#include <algorithm>

using Entity = std::uint32_t;
constexpr Entity INVALID_ENTITY = 0;

class World
{
public:
	// ---- Undo / Redo 用 ---- //
	struct IComponentBox
	{
		virtual ~IComponentBox() = default;
	};

	template<typename T>
	struct ComponentBox final : IComponentBox
	{
		explicit ComponentBox(T&& v) : value(std::move(v)) {}
		T value;
	};

	/// @brief Worldから取り外したEntity
	struct DetachedEntity
	{
		Entity id = INVALID_ENTITY;
		std::vector<std::pair<std::type_index, std::unique_ptr<IComponentBox>>> components;
	};

	/// @brief Entity を取り外す。 GPUリソース等は破棄しない
	/// @param entity 
	/// @return 
	DetachedEntity DetachEntity(Entity entity)
	{
		DetachedEntity out;
		auto it = std::find(m_Entities.begin(), m_Entities.end(), entity);
		if (it == m_Entities.end()) return out;

		m_Entities.erase(it);
		out.id = entity;
		for (auto& [type, storage] : m_Storages)
		{
			if(auto box = storage->Take(entity))
				out.components.emplace_back(type, std::move(box));
		}
		return out;
	}

	bool AttachEntity(DetachedEntity&& detached)
	{
		if (detached.id == INVALID_ENTITY || IsEntityAlive(detached.id)) return false;

		m_Entities.push_back(detached.id);
		for (auto& [type, box] : detached.components)
		{
			auto it = m_Storages.find(type);
			if (it != m_Storages.end())
			{
				it->second->Put(detached.id, std::move(box));
			}
		}
		detached.components.clear();
		detached.id = INVALID_ENTITY;
		return true;
	}

	Entity CreateEntity()
	{
		Entity entity = m_NextEntityId++;
		m_Entities.push_back(entity);
		return entity;
	}

	void DestroyEntity(Entity entity)
	{
		// 引数で受け取ったEntityが存在するか確認
		auto it = std::find(m_Entities.begin(), m_Entities.end(), entity);
		// 存在しない場合はこれ以上処理しない
		if (it == m_Entities.end())
		{
			return;
		}

		// 存在する場合はEntityを削除
		m_Entities.erase(it);
		for (auto& [_, storage] : m_Storages)
		{
			storage->Remove(entity);
		}
	}

	template<typename T>
	T& AddComponent(Entity entity, T component)
	{
		auto& storage = GetOrCreateStorage<T>();
		storage.Data[entity] = std::move(component);
		return storage.Data[entity];
	}

	template<typename T>
	void DeleteComponent(Entity entity)
	{
		auto& storage = GetOrCreateStorage<T>();
		storage.Remove(entity);
	}

	template<typename T>
	bool HasComponent(Entity entity) const
	{
		const auto* storage = TryGetStorage<T>();
		if (storage == nullptr) {
			return false;
		}
		return storage->Data.find(entity) != storage->Data.end();
	}

	template<typename T>
	T& GetComponent(Entity entity)
	{
		auto* storage = TryGetStorage<T>();
		return storage->Data.at(entity);
	}

	template<typename T>
	const T& GetComponent(Entity entity) const
	{
		const auto* storage = TryGetStorage<T>();
		return storage->Data.at(entity);
	}

	template<typename TFirst, typename... TRest, typename Func>
	/// @brief 指定したコンポーネントを持つ全てのEntityに対して関数を実行する
	void Each(Func&& func)
	{
		auto* firstStorage = TryGetStorage<TFirst>();
		if (firstStorage == nullptr) {
			return;
		}

		for (auto& [entity, firstComponent] : firstStorage->Data) {
			if ((HasComponent<TRest>(entity) && ...)) {
				func(entity, firstComponent, GetComponent<TRest>(entity)...);
			}
		}
	}

	const std::vector<Entity>& GetEntities() const
	{
		return m_Entities;
	}

	bool IsEntityAlive(Entity entity) const
	{
		return std::find(m_Entities.begin(), m_Entities.end(), entity) != m_Entities.end();
	}

	/// @brief 破棄を予約する
	/// @note スクリプトの更新中に DestroyEntity を直接呼ぶと
	///       走査中のストレージが壊れるので、フレーム末にまとめて消す
	void DestroyEntityDeferred(Entity entity)
	{
		if (entity == INVALID_ENTITY) return;
		if (std::find(m_PendingDestroy.begin(), m_PendingDestroy.end(), entity)
			!= m_PendingDestroy.end())
		{
			return;   // 二重予約は無視
		}
		m_PendingDestroy.push_back(entity);
	}

	/// @brief 予約されている破棄があるか
	bool HasPendingDestroy() const { return !m_PendingDestroy.empty(); }

	/// @brief その Entity が破棄予約されているか
	bool IsPendingDestroy(Entity entity) const
	{
		return std::find(m_PendingDestroy.begin(), m_PendingDestroy.end(), entity)
			!= m_PendingDestroy.end();
	}

	/// @brief 予約された破棄をまとめて実行する
	/// @note 破棄の連鎖に備えてキューを取り出してから回す
	void FlushDestroyQueue()
	{
		while (!m_PendingDestroy.empty())
		{
			std::vector<Entity> batch;
			batch.swap(m_PendingDestroy);
			for (Entity e : batch)
			{
				DestroyEntity(e);
			}
		}
	}

	void Clear()
	{
		m_Entities.clear();
		m_Storages.clear();
		m_PendingDestroy.clear();
		m_NextEntityId = 1;
	}

private:
	struct IStorage
	{
		virtual ~IStorage() = default;
		virtual void Remove(Entity entity) = 0;
		/// @brief 取り出して所有権ごと返す(持っていなければ nullptr)
		virtual std::unique_ptr<IComponentBox> Take(Entity entity) = 0;
		/// @brief Take したものを戻す
		virtual void Put(Entity entity, std::unique_ptr<IComponentBox> box) = 0;
	};

	template<typename T>
	struct Storage final : IStorage
	{
		std::unordered_map<Entity, T> Data;

		void Remove(Entity entity) override
		{
			Data.erase(entity);
		}

		std::unique_ptr<IComponentBox> Take(Entity entity) override
		{
			auto it = Data.find(entity);
			if (it == Data.end()) return nullptr;
			auto box = std::make_unique<ComponentBox<T>>(std::move(it->second));
			Data.erase(it);
			return box;
		}

		void Put(Entity entity, std::unique_ptr<IComponentBox> box) override
		{
			Data[entity] = std::move(static_cast<ComponentBox<T>*>(box.get())->value);
		}
	};

	template<typename T>
	Storage<T>& GetOrCreateStorage()
	{
		const auto key = std::type_index(typeid(T));
		auto it = m_Storages.find(key);
		if (it == m_Storages.end()) {
			auto created = std::make_unique<Storage<T>>();
			auto* raw = created.get();
			m_Storages[key] = std::move(created);
			return *raw;
		}

		return *static_cast<Storage<T>*>(it->second.get());
	}

	template<typename T>
	Storage<T>* TryGetStorage()
	{
		const auto key = std::type_index(typeid(T));
		auto it = m_Storages.find(key);
		if (it == m_Storages.end()) {
			return nullptr;
		}

		return static_cast<Storage<T>*>(it->second.get());
	}

	template<typename T>
	const Storage<T>* TryGetStorage() const
	{
		const auto key = std::type_index(typeid(T));
		auto it = m_Storages.find(key);
		if (it == m_Storages.end()) {
			return nullptr;
		}

		return static_cast<const Storage<T>*>(it->second.get());
	}

private:
	Entity m_NextEntityId = 1;
	std::vector<Entity> m_Entities;
	std::vector<Entity> m_PendingDestroy;   // DestroyEntityDeferred の予約
	std::unordered_map<std::type_index, std::unique_ptr<IStorage>> m_Storages;
};