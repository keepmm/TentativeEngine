/**
 * \file ShaderWatcher.hpp
 * \brief シェーダーのフォルダの変更を見張る（ホットリロード用）
 */
#pragma once

#include <filesystem>
#include <system_error>
#include <utility>

// フォルダの中の .hlsl / .hlsli のうち、一番新しい更新時刻を見張る。
// どのファイルが変わっても「全部変わった」と扱う（パイプラインが 1 本のうちは、どれが何を #include しているかを覚える必要がない）
class ShaderWatcher {
public:
    explicit ShaderWatcher(std::filesystem::path directory)
        : m_Directory(std::move(directory)), m_Stamp(LatestWriteTime()) {}

    // 変更を見つけて、さらに次の確認まで時刻が動かなかったフレームだけ true を返す。
    // 保存した直後はエディタがまだ書き込んでいることがあるので、すぐには読まない（デバウンス）
    bool Poll(float deltaSeconds) {
        m_Elapsed += deltaSeconds;
        if (m_Elapsed < kInterval) {
            return false;
        }
        m_Elapsed = 0.0f;

        const std::filesystem::file_time_type stamp = LatestWriteTime();
        if (stamp != m_Stamp) {
            m_Stamp = stamp;
            m_Pending = true;  // 落ち着くのを待つ
            return false;
        }
        if (m_Pending) {
            m_Pending = false;
            return true;
        }
        return false;
    }

private:
    // 毎フレーム見るとファイルシステムへの問い合わせが無駄に多いので、0.5 秒おきにする
    static constexpr float kInterval = 0.5f;

    std::filesystem::file_time_type LatestWriteTime() const {
        std::filesystem::file_time_type latest{};
        // 例外を投げない版を使う。保存中でファイルがロックされていても止まらないように
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(m_Directory, error)) {
            const std::filesystem::path extension = entry.path().extension();
            if (extension != ".hlsl" && extension != ".hlsli") {
                continue;
            }
            const auto time = entry.last_write_time(error);
            if (!error && time > latest) {
                latest = time;
            }
        }
        return latest;
    }

    std::filesystem::path m_Directory;
    std::filesystem::file_time_type m_Stamp;
    float m_Elapsed = 0.0f;
    bool m_Pending = false;
};
