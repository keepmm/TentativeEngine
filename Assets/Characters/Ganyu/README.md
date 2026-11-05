# Ganyu（甘雨）風キャラクターモデル

参考画像 4 枚をもとに、Blender 5.2 の Python スクリプトで手続き的に生成したゲーム用モデルです。
見た目の比較は `Preview/Comparison.png`（上段：参考画像、下段：本モデル）を参照してください。

## ファイル

| パス | 内容 |
|---|---|
| `Ganyu.fbx` | ゲーム用。メッシュ 4 つ＋アーマチュア＋`Idle` アニメーション。テクスチャ埋め込み済み |
| `Ganyu.blend` | Blender 用。トゥーン／タイツ専用シェーダーで表示（EEVEE） |
| `Textures/` | PNG テクスチャ（`T_Body_TightsMask.png` 以外は FBX にも埋め込み済み） |
| `Preview/` | プレビュー画像（参考画像と同じ 4 アングル、顔・胴・タイツのアップ、レストポーズ） |
| `Source/` | 生成スクリプト一式（入口は `build.py`） |

## 仕様

- 身長：約 1.63 m（頭頂の髪まで、ヒール約 5 cm 込み。アホ毛・角を含めると 1.67 m）
- レストポーズ：A ポーズ（腕は水平から 40° 下）
- 三角形：約 10.3 万（Body 2.2 万 / Head 0.6 万 / Hair 3.5 万 / Outfit 4.1 万）
- ボーン：70 本（Root 1 + ヒューマノイド 52 + 揺れもの 17）
- スキンウェイト：1 頂点あたり最大 4 ボーン、合計 1.0 に正規化済み
- マテリアル：19、テクスチャ：12 枚

### メッシュ

| メッシュ | 中身 | マテリアル |
|---|---|---|
| Body | 素体（肌＋タイツ／ボディスーツ）、手袋 | M_Skin, M_Tights, M_Glove |
| Head | 顔（目・眉・口はテクスチャ） | M_Face |
| Hair | 髪、角、髪留め | M_Hair, M_Horn, M_HairTie, M_BlackCloth |
| Outfit | 前垂れ、背面パネル、尻尾状の飾り、袖、襟と鈴、胸下の紐、太ももベルト、靴、房飾り、神の目 | M_FrontPanel など 11 個 |

## FBX の中身（バイナリを直接読んで確認済み）

- 単位：1 単位 = 1 m（ヘッダの UnitScaleFactor は 100。FBX は cm 基準なので「1 単位 = 100 cm」の意味）
- メッシュの頂点：Y-up・右手系で格納。キャラクターは +Z 方向を向き、キャラの左手側が +X。メッシュのノード変換は単位行列なので、スキニング無しでそのまま描いても正しい向き・大きさになります
- スケルトン：ルートノード `Armature` に X 軸 −90° の回転が入っており、その子のボーン（`Root`, `Hips`, …）のローカル変換は Blender の Z-up 基準の値です。ボーンのグローバル行列はこの回転を含めて計算してください（Assimp / ufbx などは階層をたどれば自動でそうなります）。逆バインド行列（クラスタの TransformLink）は Y-up のグローバル空間です
- D3D などの左手系で使う場合は Z を反転してください（Assimp なら `aiProcess_ConvertToLeftHanded` でまとめて変換できます）
- 三角形化済み。法線・接線（タンジェント）あり
- アニメーション：テイク `Idle`（30 fps、0–60 フレームの 2 秒ループ）。腕を下ろした姿勢で、呼吸と髪・尻尾飾り・房飾りの揺れが入っています。バインドポーズは A ポーズです
- マテリアル：PBR 相当の値（ベースカラーのテクスチャまたは色、金属度、粗さ、ノーマルマップ）が入っています。参考画像のようなトゥーンの見た目はエンジン側のシェーダーで付けてください（Blender 版の `.blend` はトゥーンで表示されます）

## ボーン

- 体幹：`Root`（床）→ `Hips` → `Spine` → `Chest` → `UpperChest` → `Neck` → `Head`
- 腕：`Shoulder` → `UpperArm` → `LowerArm` → `Hand` → 指（`Thumb` / `Index` / `Middle` / `Ring` / `Little` の 1〜3）
- 脚：`UpperLeg` → `LowerLeg` → `Foot` → `Toes`
- 左右は `.L` / `.R`
- 揺れもの：`Hair_Back_1〜6`（ポニーテール）、`Hair_Side_1〜3.L/R`（顔横の長い髪）、`Tail_1〜3`（背面の尻尾状の飾り）、`Tassel_1〜2`（腰の房飾り）

## タイツの表現

FBX には普通の PBR 相当の情報しか入らないので、`M_Tights` のベースカラーには「正面から見た平均的な見え方」を焼き込んだ近似（`T_Body_BaseColor.png` のタイツ部分）が入っています。
Blender のプレビューと同じ見た目にするには、エンジン側のシェーダーで次の式を使います。

- `T_Body_TightsMask.png`：R が透け具合（伸びている太もも・膝・胸・お尻ほど大きく、足首やつま先は小さい）、G がタイツ部分のマスク
- `T_Tights_Knit_N.png`：編み目のタンジェント空間ノーマル。UV を 109.4 倍してタイリング（FBX のテクスチャスケールにも入っています）
- 定数（リニア空間）：タイツ色 (0.018, 0.011, 0.020)、透けて見える肌色 (0.80, 0.50, 0.50)、透け量 0.20

```hlsl
float ndv   = max(dot(N, V), 0.04);              // N は編み目ノーマル適用後
float sheer = 0.20 * pow(mask.r, 1.0 / ndv);     // 斜めから見るほど布が重なって暗くなる
float3 albedo = lerp(tightsColor, skinColor, sheer);
```

あとは他のマテリアルと同じトゥーンライティングをかけ、輪郭付近に弱いシーン（Fresnel の 3 乗 × 光の当たり具合）を足すと布らしさが出ます。

## 作り直す・調整する

```
"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" -b --factory-startup --python Source\build.py -- --save --export
"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" -b Ganyu.blend --python Source\render_previews.py
```

1 回のビルドは約 90 秒です。主な調整箇所は次のとおりです。

- 体型：`Source/body.py` の `TORSO_ROWS` / `LEG_ROWS`（高さごとの断面の幅・前後の奥行き）
- 顔：`Source/head.py` の `FACE_ROWS`、目の描画は `Source/facepaint.py`
- 髪：`Source/hair.py`（前髪・横髪・ポニーテールの束の配置）、色は `Source/textures.py` の `hair_texture`
- タイツの見え方：`Source/materials.py` の `tights_material`、伸び具合マップは `Source/textures.py` の `tights_clearness`

## 注意

キャラクターデザインの権利は HoYoverse にあります。個人の学習・非商用の範囲で使ってください。
