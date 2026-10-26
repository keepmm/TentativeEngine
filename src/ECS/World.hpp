/**
 * \file World.hpp
 * \brief Entity とコンポーネントをまとめて管理する入れ物（アーキタイプ方式）
 */
#pragma once

#include "ECS/ArcheType.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

namespace TE {

    class World {
    public:
        World() {
            // コンポーネントを 1 つも持たない Entity もアーキタイプ 0 番に入れて、扱いをそろえる
            m_Archetypes.push_back(std::make_unique<Archetype>(std::vector<ComponentTypeID>{}));
            m_ArchetypeLookup.emplace(std::vector<ComponentTypeID>{}, 0u);
        }

        World(const World&) = delete;
        World& operator=(const World&) = delete;

        // ---- Entity ---- //

        // コンポーネントを持たない Entity を作る
        Entity CreateEntity() {
            assert(m_IterationDepth == 0 && "Each の中で CreateEntity している。Defer を使う");
            return AllocateEntity(0);
        }

        /// @brief コンポーネントを全部付けた状態で作る。例: CreateEntity(Position{}, Velocity{ 1, 0, 0 })
        /// @note  最終的なアーキタイプに直接置くので、AddComponent を何回も呼ぶのと違い引っ越しが起きない。
        ///        たくさん作るときはこちらを使う
        template <typename... Ts>
            requires(sizeof...(Ts) > 0)
        Entity CreateEntity(Ts... components) {
            static_assert(AllDistinct<Ts...>(), "同じ型のコンポーネントが 2 つ指定されている");
            assert(m_IterationDepth == 0 && "Each の中で CreateEntity している。Defer を使う");

            // 空のアーキタイプから 1 つずつ足した先を、覚えてある行き先（addEdges）でたどる。2 回目からは探索しない。
            // 型番号の小さい順にたどると、引数の順番が違っても途中のアーキタイプが同じになり、余計なものが増えない
            std::array<ComponentTypeID, sizeof...(Ts)> ids{ ComponentTypeIDOf<Ts>()... };
            std::sort(ids.begin(), ids.end());
            std::uint32_t target = 0;
            for (const ComponentTypeID id : ids) {
                target = AddTarget(target, id);
            }

            const Entity entity = AllocateEntity(target);
            const Slot& slot = m_Slots[entity.index];
            Archetype& archetype = *m_Archetypes[target];
            Chunk& chunk = archetype.Chunks()[slot.chunk];
            // 値は引数として受け取った時点でできあがっている（コピーで例外が出ても、まだチャンクに触る前）。
            // ここでは例外を出さない移動だけをする
            (::new (archetype.ComponentAt(chunk, archetype.ColumnOf(ComponentTypeIDOf<Ts>()), slot.row)) Ts(std::move(components)), ...);
            return entity;
        }

        bool IsAlive(Entity entity) const {
            return entity.index < m_Slots.size()
                && m_Slots[entity.index].alive
                && m_Slots[entity.index].generation == entity.generation;
        }

        // すぐに破棄する。Each の中では使えない。代わりに DestroyEntityDeferred を使う
        void DestroyEntity(Entity entity) {
            assert(m_IterationDepth == 0 && "Each の中で DestroyEntity している。DestroyEntityDeferred を使う");
            if (!IsAlive(entity)) {
                return;
            }
            Slot& slot = m_Slots[entity.index];
            Archetype& archetype = *m_Archetypes[slot.archetype];
            const ChunkLocation location{ slot.chunk, slot.row };

            // 後片付けを先に全部知らせてから壊す（壊した後では中身が読めない）
            for (std::size_t column = 0; column < archetype.Types().size(); ++column) {
                NotifyRemove(entity, archetype, location, static_cast<int>(column));
            }
            Chunk& chunk = archetype.Chunks()[location.chunk];
            for (std::size_t column = 0; column < archetype.Types().size(); ++column) {
                GetComponentInfo(archetype.Types()[column]).destroy(archetype.ComponentAt(chunk, static_cast<int>(column), location.row));
            }
            FixMovedEntity(archetype.RemoveRow(location), location);

            // generation を進めておくと、手元に残った古い Entity は IsAlive で false になる
            slot.alive = false;
            ++slot.generation;
            m_FreeIndices.push_back(entity.index);
            --m_AliveCount;
        }

        std::size_t AliveCount() const { return m_AliveCount; }

        // ---- コンポーネント ---- //

        /// @brief コンポーネントを追加して参照を返す
        /// @note  Entity は「1 つ多い組み合わせ」のアーキタイプへ引っ越す。
        ///        Add / Remove / Destroy のあとは、それまでに取った参照・ポインタはすべて無効になる
        template <typename T, typename... Args>
        T& AddComponent(Entity entity, Args&&... args) {
            assert(m_IterationDepth == 0 && "Each の中で AddComponent している。AddComponentDeferred を使う");
            assert(IsAlive(entity) && "破棄済みの Entity にコンポーネントを追加している");
            const ComponentTypeID id = ComponentTypeIDOf<T>();
            assert(!m_Archetypes[m_Slots[entity.index].archetype]->Has(id) && "同じコンポーネントを 2 回追加している");

            // 先に一時オブジェクトとして作る。作るときに例外が出ても、まだ引っ越し前なので壊れない
            T value(std::forward<Args>(args)...);

            Slot& slot = m_Slots[entity.index];
            const std::uint32_t target = AddTarget(slot.archetype, id);
            MoveEntity(entity, target);

            Archetype& archetype = *m_Archetypes[target];
            Chunk& chunk = archetype.Chunks()[slot.chunk];
            return *::new (archetype.ComponentAt(chunk, archetype.ColumnOf(id), slot.row)) T(std::move(value));
        }

        template <typename T>
        void RemoveComponent(Entity entity) {
            assert(m_IterationDepth == 0 && "Each の中で RemoveComponent している。RemoveComponentDeferred を使う");
            if (!HasComponent<T>(entity)) {
                return;
            }
            const ComponentTypeID id = ComponentTypeIDOf<T>();
            Slot& slot = m_Slots[entity.index];
            Archetype& source = *m_Archetypes[slot.archetype];
            NotifyRemove(entity, source, { slot.chunk, slot.row }, source.ColumnOf(id));
            // 行き先に T の列はないので、MoveEntity の中で T は破棄される
            MoveEntity(entity, RemoveTarget(slot.archetype, id));
        }

        template <typename T>
        bool HasComponent(Entity entity) const {
            return IsAlive(entity) && m_Archetypes[m_Slots[entity.index].archetype]->Has(ComponentTypeIDOf<T>());
        }

        template <typename T>
        T& GetComponent(Entity entity) {
            T* component = TryGetComponent<T>(entity);
            assert(component != nullptr && "持っていないコンポーネントを Get している");
            return *component;
        }

        // 持っていなければ nullptr（Unity の TryGetComponent に近い）
        template <typename T>
        T* TryGetComponent(Entity entity) {
            if (!IsAlive(entity)) {
                return nullptr;
            }
            const Slot& slot = m_Slots[entity.index];
            Archetype& archetype = *m_Archetypes[slot.archetype];
            const int column = archetype.ColumnOf(ComponentTypeIDOf<T>());
            if (column < 0) {
                return nullptr;
            }
            return static_cast<T*>(archetype.ComponentAt(archetype.Chunks()[slot.chunk], column, slot.row));
        }

        template <typename T>
        std::size_t Count() const {
            const ComponentTypeID id = ComponentTypeIDOf<T>();
            std::size_t count = 0;
            for (const auto& archetype : m_Archetypes) {
                if (archetype->Has(id)) {
                    count += archetype->EntityCount();
                }
            }
            return count;
        }

        // 外されるとき（RemoveComponent・Entity の破棄・Clear）に呼ばれる関数を登録する。
        // 中では Add / Remove / Destroy はできない（引っ越しの途中なので）。必要なら Deferred 版を使う
        template <typename T>
        void SetOnRemove(std::function<void(Entity, T&)> callback) {
            const ComponentTypeID id = ComponentTypeIDOf<T>();
            if (id >= m_OnRemove.size()) {
                m_OnRemove.resize(id + 1);
            }
            if (callback) {
                m_OnRemove[id] = [callback = std::move(callback)](Entity entity, void* component) {
                    callback(entity, *static_cast<T*>(component));
                    };
            }
            else {
                m_OnRemove[id] = nullptr;
            }
        }

        // ---- 走査 ---- //

        /// @brief Ts... をすべて持ち、しかも全部「有効」な Entity に対して func(Entity, Ts&...) を呼ぶ
        /// @note  条件に合うアーキタイプのチャンクを、配列の頭から順に読むだけなので、どの組み合わせでも連続アクセスになる
        template <typename... Ts, typename Func>
        void Each(Func&& func) {
            EachChunk<Ts...>([&](std::uint32_t count, const Entity* entities, const std::uint64_t* enabledMask, Ts*... components) {
                if (enabledMask == nullptr) {
                    // 有効/無効を切り替えられる型がないので、全行をそのまま回す
                    for (std::uint32_t i = 0; i < count; ++i) {
                        func(entities[i], components[i]...);
                    }
                    return;
                }
                // 64 行ずつ、立っているビット（有効な行）だけを拾う。全部無効の 64 行は 1 回の比較で飛ばせる
                for (std::uint32_t word = 0; word * 64 < count; ++word) {
                    std::uint64_t bits = enabledMask[word];
                    while (bits != 0) {
                        const std::uint32_t i = word * 64 + static_cast<std::uint32_t>(std::countr_zero(bits));
                        func(entities[i], components[i]...);
                        bits &= bits - 1;  // 一番下の 1 を消す
                    }
                }
                });
        }

        /// @brief チャンク単位で func(行数, Entity の配列, 有効マスク, Ts の配列...) を呼ぶ
        /// @param enabledMask 行ごとに「Ts の切り替えられる型が全部有効か」のビット（64 行で 1 単語）。
        ///                    Ts に切り替えられる型がなければ nullptr（全行が対象）
        /// @note  配列をそのまま渡すので、SIMD でまとめて計算したり、チャンクごとにスレッドへ配ったりできる
        template <typename... Ts, typename Func>
        void EachChunk(Func&& func) {
            static_assert(sizeof...(Ts) > 0, "少なくとも 1 つコンポーネントの型を指定する");
            const ComponentTypeID ids[] = { ComponentTypeIDOf<Ts>()... };
            constexpr bool kAnyEnableable = (kIsEnableable<Ts> || ...);

            IterationGuard guard(m_IterationDepth);
            for (const auto& archetype : m_Archetypes) {
                if (archetype->EntityCount() == 0
                    || !std::all_of(std::begin(ids), std::end(ids), [&](ComponentTypeID id) { return archetype->Has(id); })) {
                    continue;
                }
                for (Chunk& chunk : archetype->Chunks()) {
                    if constexpr (kAnyEnableable) {
                        // 切り替えられる型の有効ビットを全部 AND して、「全部有効な行」のマスクを作る
                        std::uint64_t mask[kMaxMaskWords];
                        const std::uint32_t words = (chunk.count + 63) / 64;
                        for (std::uint32_t w = 0; w < words; ++w) {
                            mask[w] = ~std::uint64_t{ 0 };
                        }
                        for (const ComponentTypeID id : ids) {
                            if (const std::uint64_t* bits = archetype->EnabledBits(chunk, archetype->ColumnOf(id))) {
                                for (std::uint32_t w = 0; w < words; ++w) {
                                    mask[w] &= bits[w];
                                }
                            }
                        }
                        // 最後の単語の、行のない部分のビットを消す
                        if (chunk.count % 64 != 0) {
                            mask[words - 1] &= (std::uint64_t{ 1 } << (chunk.count % 64)) - 1;
                        }
                        func(chunk.count, Archetype::Entities(chunk), static_cast<const std::uint64_t*>(mask), archetype->Column<Ts>(chunk)...);
                    }
                    else {
                        func(chunk.count, Archetype::Entities(chunk), static_cast<const std::uint64_t*>(nullptr), archetype->Column<Ts>(chunk)...);
                    }
                }
            }
        }

        // ---- 有効/無効（kEnableable を持つ型だけ）---- //
        // 付け外しと違って引っ越しが起きない。無効のあいだも値は残り、HasComponent / GetComponent はそのまま使える。
        // Each は無効なものを飛ばす。Each の中から呼んでもよい（ビットを書き換えるだけで、配列は動かないため）

        template <typename T>
        void SetEnabled(Entity entity, bool enabled) {
            static_assert(kIsEnableable<T>, "有効/無効を切り替えるには、型に static constexpr bool kEnableable = true; を書く");
            assert(HasComponent<T>(entity) && "持っていないコンポーネントの有効/無効を切り替えている");
            const Slot& slot = m_Slots[entity.index];
            Archetype& archetype = *m_Archetypes[slot.archetype];
            archetype.SetEnabled(archetype.Chunks()[slot.chunk], archetype.ColumnOf(ComponentTypeIDOf<T>()), slot.row, enabled);
        }

        // 持っていなければ false
        template <typename T>
        bool IsEnabled(Entity entity) {
            static_assert(kIsEnableable<T>, "有効/無効を切り替えるには、型に static constexpr bool kEnableable = true; を書く");
            if (!HasComponent<T>(entity)) {
                return false;
            }
            const Slot& slot = m_Slots[entity.index];
            Archetype& archetype = *m_Archetypes[slot.archetype];
            return archetype.IsEnabled(archetype.Chunks()[slot.chunk], archetype.ColumnOf(ComponentTypeIDOf<T>()), slot.row);
        }

        // ---- 遅延操作（コマンドバッファ）---- //
        // Each の中では構造を変えられない（チャンクの中身が動いて、走査中の配列が壊れるため）。
        // 代わりに「やりたいこと」をためておき、FlushDeferred でまとめて実行する（フレームの終わりなど）

        void DestroyEntityDeferred(Entity entity) {
            m_Deferred.push_back([entity](World& world) { world.DestroyEntity(entity); });  // 二重に予約しても、2 回目は IsAlive で弾かれる
        }

        // すでに持っていれば値を上書きする
        template <typename T>
        void AddComponentDeferred(Entity entity, T value) {
            m_Deferred.push_back([entity, value = std::move(value)](World& world) mutable {
                if (!world.IsAlive(entity)) {
                    return;
                }
                if (T* existing = world.TryGetComponent<T>(entity)) {
                    *existing = std::move(value);
                }
                else {
                    world.AddComponent<T>(entity, std::move(value));
                }
                });
        }

        template <typename T>
        void RemoveComponentDeferred(Entity entity) {
            m_Deferred.push_back([entity](World& world) { world.RemoveComponent<T>(entity); });
        }

        // 何でも後回しにできる（Entity を新しく作る、など）
        void Defer(std::function<void(World&)> command) { m_Deferred.push_back(std::move(command)); }

        void FlushDeferred() {
            assert(m_IterationDepth == 0 && "Each の中で FlushDeferred している");
            // 実行した操作がさらに予約することがあるので、空になるまで繰り返す
            while (!m_Deferred.empty()) {
                std::vector<std::function<void(World&)>> batch;
                batch.swap(m_Deferred);
                for (auto& command : batch) {
                    command(*this);
                }
            }
        }

        // すべての Entity とコンポーネントを捨てる（シーンの切り替えなど）。アーキタイプの並び方は覚えたまま残す
        void Clear() {
            assert(m_IterationDepth == 0 && "Each の中で Clear している");
            for (const auto& archetype : m_Archetypes) {
                for (std::uint32_t chunkIndex = 0; chunkIndex < archetype->Chunks().size(); ++chunkIndex) {
                    const Chunk& chunk = archetype->Chunks()[chunkIndex];
                    for (std::uint32_t row = 0; row < chunk.count; ++row) {
                        const Entity entity = Archetype::Entities(archetype->Chunks()[chunkIndex])[row];
                        for (std::size_t column = 0; column < archetype->Types().size(); ++column) {
                            NotifyRemove(entity, *archetype, { chunkIndex, row }, static_cast<int>(column));
                        }
                    }
                }
            }
            for (const auto& archetype : m_Archetypes) {
                archetype->DestroyAllRows();
            }
            // 番号は使い回すので、generation は残したまま全部を空きにする
            m_FreeIndices.clear();
            for (std::uint32_t index = 0; index < m_Slots.size(); ++index) {
                if (m_Slots[index].alive) {
                    m_Slots[index].alive = false;
                    ++m_Slots[index].generation;
                }
                m_FreeIndices.push_back(index);
            }
            m_Deferred.clear();
            m_AliveCount = 0;
        }

        // ---- メモリの見える化 ---- //
        std::size_t ArchetypeCount() const { return m_Archetypes.size(); }
        std::size_t ChunkCount() const {
            std::size_t count = 0;
            for (const auto& archetype : m_Archetypes) {
                count += archetype->Chunks().size();
            }
            return count;
        }
        std::size_t ChunkMemoryBytes() const { return ChunkCount() * kChunkSize; }

    private:
        // Entity の番号ごとの情報。どのアーキタイプの、どのチャンクの、何行目にいるか
        struct Slot {
            std::uint32_t generation = 0;
            bool alive = false;
            std::uint32_t archetype = 0;
            std::uint32_t chunk = 0;
            std::uint32_t row = 0;
        };

        // Each の中かどうかを数える。例外で抜けても戻るよう、デストラクタで減らす
        struct IterationGuard {
            explicit IterationGuard(int& depth) : m_Depth(depth) { ++m_Depth; }
            ~IterationGuard() { --m_Depth; }
            IterationGuard(const IterationGuard&) = delete;
            IterationGuard& operator=(const IterationGuard&) = delete;
            int& m_Depth;
        };

        // 番号を決めて、指定のアーキタイプに行を確保する（コンポーネントは呼び出し側が作る）
        Entity AllocateEntity(std::uint32_t archetypeIndex) {
            Entity entity;
            // 空いている番号があれば使い回す。generation は破棄のときに増やしてある
            if (!m_FreeIndices.empty()) {
                entity.index = m_FreeIndices.back();
                m_FreeIndices.pop_back();
                entity.generation = m_Slots[entity.index].generation;
            }
            else {
                entity.index = static_cast<std::uint32_t>(m_Slots.size());
                m_Slots.emplace_back();
            }
            Slot& slot = m_Slots[entity.index];
            slot.alive = true;
            slot.archetype = archetypeIndex;
            const ChunkLocation location = m_Archetypes[archetypeIndex]->AllocateRow(entity);
            slot.chunk = location.chunk;
            slot.row = location.row;
            ++m_AliveCount;
            return entity;
        }

        // 型の並びに重複がないか（CreateEntity(Position{}, Position{}) を防ぐ）
        template <typename... Ts>
        static constexpr bool AllDistinct() {
            if constexpr (sizeof...(Ts) <= 1) {
                return true;
            }
            else {
                return AllDistinctImpl<Ts...>();
            }
        }
        template <typename First, typename... Rest>
        static constexpr bool AllDistinctImpl() {
            return (!std::is_same_v<First, Rest> && ...) && AllDistinct<Rest...>();
        }

        // 型の組み合わせからアーキタイプを探す。なければ作る
        std::uint32_t FindOrCreateArchetype(const std::vector<ComponentTypeID>& types) {
            const auto it = m_ArchetypeLookup.find(types);
            if (it != m_ArchetypeLookup.end()) {
                return it->second;
            }
            const auto index = static_cast<std::uint32_t>(m_Archetypes.size());
            m_Archetypes.push_back(std::make_unique<Archetype>(types));
            m_ArchetypeLookup.emplace(types, index);
            return index;
        }

        // source に id を足した組み合わせ
        std::uint32_t AddTarget(std::uint32_t source, ComponentTypeID id) {
            auto& edges = m_Archetypes[source]->addEdges;
            if (const auto it = edges.find(id); it != edges.end()) {
                return it->second;
            }
            std::vector<ComponentTypeID> types = m_Archetypes[source]->Types();
            types.insert(std::upper_bound(types.begin(), types.end(), id), id);  // 小さい順を保つ
            const std::uint32_t target = FindOrCreateArchetype(types);
            m_Archetypes[source]->addEdges.emplace(id, target);  // FindOrCreate で配列が伸びても、Archetype 本体は動かない
            return target;
        }

        // source から id を引いた組み合わせ
        std::uint32_t RemoveTarget(std::uint32_t source, ComponentTypeID id) {
            auto& edges = m_Archetypes[source]->removeEdges;
            if (const auto it = edges.find(id); it != edges.end()) {
                return it->second;
            }
            std::vector<ComponentTypeID> types = m_Archetypes[source]->Types();
            types.erase(std::find(types.begin(), types.end(), id));
            const std::uint32_t target = FindOrCreateArchetype(types);
            m_Archetypes[source]->removeEdges.emplace(id, target);
            return target;
        }

        // Entity を別のアーキタイプへ引っ越す。
        // 両方にある型は移動し、行き先にない型は破棄する。行き先にしかない型の場所は未初期化のまま（呼び出し側が作る）
        void MoveEntity(Entity entity, std::uint32_t targetIndex) {
            Slot& slot = m_Slots[entity.index];
            Archetype& source = *m_Archetypes[slot.archetype];
            Archetype& target = *m_Archetypes[targetIndex];
            const ChunkLocation from{ slot.chunk, slot.row };
            const ChunkLocation to = target.AllocateRow(entity);

            Chunk& fromChunk = source.Chunks()[from.chunk];
            Chunk& toChunk = target.Chunks()[to.chunk];
            for (std::size_t column = 0; column < source.Types().size(); ++column) {
                const ComponentTypeID id = source.Types()[column];
                const ComponentInfo& info = GetComponentInfo(id);
                void* component = source.ComponentAt(fromChunk, static_cast<int>(column), from.row);
                if (const int targetColumn = target.ColumnOf(id); targetColumn >= 0) {
                    info.moveConstruct(target.ComponentAt(toChunk, targetColumn, to.row), component);
                    if (info.enableable) {
                        // 無効にしてあったものは、引っ越し先でも無効のまま
                        target.SetEnabled(toChunk, targetColumn, to.row, source.IsEnabled(fromChunk, static_cast<int>(column), from.row));
                    }
                }
                info.destroy(component);
            }
            FixMovedEntity(source.RemoveRow(from), from);

            slot.archetype = targetIndex;
            slot.chunk = to.chunk;
            slot.row = to.row;
        }

        // RemoveRow で穴を埋めるために動いた Entity の居場所を書き換える
        void FixMovedEntity(Entity moved, ChunkLocation newLocation) {
            if (!moved.IsNull()) {
                m_Slots[moved.index].chunk = newLocation.chunk;
                m_Slots[moved.index].row = newLocation.row;
            }
        }

        void NotifyRemove(Entity entity, Archetype& archetype, ChunkLocation location, int column) {
            const ComponentTypeID id = archetype.Types()[column];
            if (id < m_OnRemove.size() && m_OnRemove[id]) {
                // 後片付けの中で構造を変えられると、引っ越しの途中で壊れるので Each と同じく禁止する
                IterationGuard guard(m_IterationDepth);
                m_OnRemove[id](entity, archetype.ComponentAt(archetype.Chunks()[location.chunk], column, location.row));
            }
        }

        std::vector<Slot> m_Slots;
        std::vector<std::uint32_t> m_FreeIndices;
        std::vector<std::unique_ptr<Archetype>> m_Archetypes;  // unique_ptr なので、配列が伸びても Archetype 本体は動かない
        std::map<std::vector<ComponentTypeID>, std::uint32_t> m_ArchetypeLookup;
        std::vector<std::function<void(Entity, void*)>> m_OnRemove;  // 添字 = 型番号
        std::vector<std::function<void(World&)>> m_Deferred;
        std::size_t m_AliveCount = 0;
        int m_IterationDepth = 0;
    };

}  // namespace TE