/**
 * \file Archetype.hpp
 * \brief アーキタイプ（コンポーネントの組み合わせ）と、その Entity を詰めておくチャンク
 */
#pragma once

#include "ECS/ComponentType.hpp"
#include "ECS/Entity.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <unordered_map>
#include <vector>

namespace TE {

    // チャンク 1 つの大きさ。L1 キャッシュ（数十 KB）に収まりやすく、Unity DOTS と同じ 16KB にする
    inline constexpr std::size_t kChunkSize = 16 * 1024;
    // チャンクの先頭をキャッシュラインの境目にそろえる
    inline constexpr std::size_t kChunkAlignment = 64;
    // 1 チャンクの行数の上限（Entity だけのときが最大）を 64 ビットの単語で表すのに必要な数
    inline constexpr std::size_t kMaxMaskWords = (kChunkSize / sizeof(Entity) + 63) / 64;

    struct ChunkDeleter {
        void operator()(std::byte* data) const { ::operator delete(data, std::align_val_t{ kChunkAlignment }); }
    };

    // 同じアーキタイプの Entity を capacity 個まで入れる、固定サイズのメモリの塊。
    // 中は型ごとに配列を分けて並べる（SoA: Structure of Arrays）。
    //   [Entity × capacity][コンポーネント A × capacity][コンポーネント B × capacity] ... [有効ビット（有効/無効を切り替えられる型の分）]
    // System は自分が使う型の配列だけを頭から読めるので、使わないデータでキャッシュが汚れない
    struct Chunk {
        std::unique_ptr<std::byte[], ChunkDeleter> data;
        std::uint32_t count = 0;  // 使っている行数。行は常に前から詰まっている
    };

    // 行の場所（何番目のチャンクの何行目か）
    struct ChunkLocation {
        std::uint32_t chunk = 0;
        std::uint32_t row = 0;
    };

    // 「同じコンポーネントの組み合わせを持つ Entity の集まり」。
    // 組み合わせが同じなら行の大きさも同じなので、チャンクの中に隙間なく並べられる
    class Archetype {
    public:
        // types はコンポーネントの型番号を小さい順に並べたもの
        explicit Archetype(std::vector<ComponentTypeID> types) : m_Types(std::move(types)) {
            for (std::size_t column = 0; column < m_Types.size(); ++column) {
                const ComponentTypeID id = m_Types[column];
                if (id >= m_ColumnOf.size()) {
                    m_ColumnOf.resize(id + 1, -1);
                }
                m_ColumnOf[id] = static_cast<int>(column);
                m_Infos.push_back(GetComponentInfo(id));
                if (m_Infos.back().enableable) {
                    m_EnableableColumns.push_back(static_cast<int>(column));
                }
            }
            ComputeLayout();
        }

        ~Archetype() { DestroyAllRows(); }

        Archetype(const Archetype&) = delete;
        Archetype& operator=(const Archetype&) = delete;

        const std::vector<ComponentTypeID>& Types() const { return m_Types; }

        // その型が何列目か。持っていなければ -1
        int ColumnOf(ComponentTypeID id) const { return id < m_ColumnOf.size() ? m_ColumnOf[id] : -1; }
        bool Has(ComponentTypeID id) const { return ColumnOf(id) >= 0; }

        std::uint32_t Capacity() const { return m_Capacity; }
        std::size_t EntityCount() const { return m_EntityCount; }
        std::vector<Chunk>& Chunks() { return m_Chunks; }
        const std::vector<Chunk>& Chunks() const { return m_Chunks; }

        // ---- チャンクの中の配列 ---- //

        static Entity* Entities(Chunk& chunk) { return reinterpret_cast<Entity*>(chunk.data.get()); }

        void* ComponentAt(Chunk& chunk, int column, std::uint32_t row) {
            return chunk.data.get() + m_Offsets[column] + m_Infos[column].size * row;
        }

        // T の配列の先頭。この Archetype が T を持っていること
        template <typename T>
        T* Column(Chunk& chunk) {
            const int column = ColumnOf(ComponentTypeIDOf<T>());
            assert(column >= 0);
            return reinterpret_cast<T*>(chunk.data.get() + m_Offsets[column]);
        }

        // ---- 有効ビット ---- //
        // 有効/無効を切り替えられる型の列だけ、行ごとに 1 ビット持つ（1 = 有効）。
        // 64 行分を 1 つの uint64_t にまとめるので、Each では 64 行ずつまとめて判定できる

        // その列の有効ビットの先頭。切り替えられない型なら nullptr
        std::uint64_t* EnabledBits(Chunk& chunk, int column) {
            return m_Infos[column].enableable ? reinterpret_cast<std::uint64_t*>(chunk.data.get() + m_BitOffsets[column]) : nullptr;
        }

        bool IsEnabled(Chunk& chunk, int column, std::uint32_t row) {
            const std::uint64_t* bits = EnabledBits(chunk, column);
            return bits == nullptr || ((bits[row / 64] >> (row % 64)) & 1u) != 0;
        }

        void SetEnabled(Chunk& chunk, int column, std::uint32_t row, bool enabled) {
            std::uint64_t* bits = EnabledBits(chunk, column);
            assert(bits != nullptr && "有効/無効を切り替えられない型（kEnableable がない）");
            const std::uint64_t mask = std::uint64_t{ 1 } << (row % 64);
            bits[row / 64] = enabled ? (bits[row / 64] | mask) : (bits[row / 64] & ~mask);
        }

        // ---- 行の追加・削除 ---- //

        // 末尾に 1 行足して Entity を書き込む。コンポーネントの場所は未初期化のまま（呼び出し側が作る）。
        // 有効ビットは「有効」から始まる（Unity と同じ）
        ChunkLocation AllocateRow(Entity entity) {
            if (m_Chunks.empty() || m_Chunks.back().count == m_Capacity) {
                Chunk chunk;
                chunk.data.reset(static_cast<std::byte*>(::operator new(kChunkSize, std::align_val_t{ kChunkAlignment })));
                m_Chunks.push_back(std::move(chunk));
            }
            const auto chunkIndex = static_cast<std::uint32_t>(m_Chunks.size() - 1);
            Chunk& chunk = m_Chunks.back();
            const std::uint32_t row = chunk.count++;
            Entities(chunk)[row] = entity;
            for (const int column : m_EnableableColumns) {
                SetEnabled(chunk, column, row, true);
            }
            ++m_EntityCount;
            return { chunkIndex, row };
        }

        /// @brief 行を消して、空いた穴をアーキタイプ全体の最後の行で埋める（swap and pop）
        /// @note  その行のコンポーネントは、呼び出し側ですでに破棄（または移動）しておくこと
        /// @return 穴を埋めるために移った Entity（場所を書き換える必要がある）。移らなければ kNullEntity
        Entity RemoveRow(ChunkLocation location) {
            Chunk& lastChunk = m_Chunks.back();
            const auto lastChunkIndex = static_cast<std::uint32_t>(m_Chunks.size() - 1);
            const std::uint32_t lastRow = lastChunk.count - 1;

            Entity moved = kNullEntity;
            if (location.chunk != lastChunkIndex || location.row != lastRow) {
                Chunk& hole = m_Chunks[location.chunk];
                for (std::size_t column = 0; column < m_Infos.size(); ++column) {
                    const int c = static_cast<int>(column);
                    void* source = ComponentAt(lastChunk, c, lastRow);
                    m_Infos[column].moveConstruct(ComponentAt(hole, c, location.row), source);
                    m_Infos[column].destroy(source);
                }
                // 有効ビットも一緒に移す
                for (const int column : m_EnableableColumns) {
                    SetEnabled(hole, column, location.row, IsEnabled(lastChunk, column, lastRow));
                }
                moved = Entities(lastChunk)[lastRow];
                Entities(hole)[location.row] = moved;
            }

            --lastChunk.count;
            --m_EntityCount;
            // 空になったチャンクはすぐ返す。行は常に前から詰まっているので、空になるのは最後のチャンクだけ
            if (lastChunk.count == 0) {
                m_Chunks.pop_back();
            }
            return moved;
        }

        // 全部の行のコンポーネントを破棄して、チャンクも返す
        void DestroyAllRows() {
            for (Chunk& chunk : m_Chunks) {
                for (std::uint32_t row = 0; row < chunk.count; ++row) {
                    for (std::size_t column = 0; column < m_Infos.size(); ++column) {
                        m_Infos[column].destroy(ComponentAt(chunk, static_cast<int>(column), row));
                    }
                }
            }
            m_Chunks.clear();
            m_EntityCount = 0;
        }

        // コンポーネントを 1 つ足した / 引いたときの行き先アーキタイプの番号。毎回探さないよう覚えておく
        std::unordered_map<ComponentTypeID, std::uint32_t> addEdges;
        std::unordered_map<ComponentTypeID, std::uint32_t> removeEdges;

    private:
        static constexpr std::size_t AlignUp(std::size_t value, std::size_t alignment) {
            return (value + alignment - 1) / alignment * alignment;
        }

        // capacity 個入れたときに必要なバイト数を求め、各配列の開始位置を決める
        std::size_t LayoutBytes(std::uint32_t capacity, std::vector<std::size_t>* offsets, std::vector<std::size_t>* bitOffsets) const {
            std::size_t offset = sizeof(Entity) * capacity;
            for (const ComponentInfo& info : m_Infos) {
                offset = AlignUp(offset, info.alignment);  // 各配列の先頭を、その型の並びの境目にそろえる
                if (offsets != nullptr) {
                    offsets->push_back(offset);
                }
                offset += info.size * capacity;
            }
            // 有効ビットは、コンポーネントの配列の後ろにまとめて置く
            const std::size_t bitBytes = (capacity + 63) / 64 * sizeof(std::uint64_t);
            for (const ComponentInfo& info : m_Infos) {
                if (!info.enableable) {
                    if (bitOffsets != nullptr) {
                        bitOffsets->push_back(0);
                    }
                    continue;
                }
                offset = AlignUp(offset, alignof(std::uint64_t));
                if (bitOffsets != nullptr) {
                    bitOffsets->push_back(offset);
                }
                offset += bitBytes;
            }
            return offset;
        }

        void ComputeLayout() {
            // まず 1 行あたりの大きさで割って目安を出し、境目そろえの隙間で溢れるなら 1 つずつ減らす
            std::size_t bytesPerRow = sizeof(Entity);
            for (const ComponentInfo& info : m_Infos) {
                bytesPerRow += info.size;
            }
            auto capacity = static_cast<std::uint32_t>(kChunkSize / bytesPerRow);
            while (capacity > 0 && LayoutBytes(capacity, nullptr, nullptr) > kChunkSize) {
                --capacity;
            }
            assert(capacity > 0 && "コンポーネントが大きすぎて、1 チャンクに 1 行も入らない");
            m_Capacity = capacity;
            m_Offsets.clear();
            m_BitOffsets.clear();
            LayoutBytes(capacity, &m_Offsets, &m_BitOffsets);
        }

        std::vector<ComponentTypeID> m_Types;   // 列 → 型番号（小さい順）
        std::vector<int> m_ColumnOf;            // 型番号 → 列（なければ -1）
        std::vector<ComponentInfo> m_Infos;     // 列ごとの型情報
        std::vector<std::size_t> m_Offsets;     // 列ごとの、チャンク先頭からの配列の位置
        std::vector<std::size_t> m_BitOffsets;  // 列ごとの、有効ビットの位置（切り替えられない型は使わない）
        std::vector<int> m_EnableableColumns;   // 有効/無効を切り替えられる列
        std::uint32_t m_Capacity = 0;           // 1 チャンクに入る行数
        std::vector<Chunk> m_Chunks;
        std::size_t m_EntityCount = 0;
    };

}  // namespace TE