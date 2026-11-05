/**
 * \file EngineTypes.hpp
 * \brief エンジンのコンポーネントを TypeRegistry に登録する
 */
#pragma once

namespace TE { class TypeRegistry; }

// 起動時に 1 回だけ呼ぶ。静的初期化の順番に頼らず、ここで全部の型番号を確定させる
void RegisterEngineTypes(TE::TypeRegistry& registry);
