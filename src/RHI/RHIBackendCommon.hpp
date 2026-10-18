/**
 * \file RHIBackendCommon.hpp
 * \brief RHI のバックエンド実装（D3D11 / D3D12 ...）だけが include する共通ヘッダ
 */
#pragma once

// RHI の外（Renderer / Engine）からは include しない。
// 外に見せるのは IRHI*.hpp と RHITypes.hpp だけにして、Windows や D3D の型が漏れないようにする
#include "Core/Defines.hpp"
#include "Core/Log.hpp"

#include <cassert>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
