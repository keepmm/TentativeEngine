cmake_minimum_required(VERSION 3.20)
project(TentativeEngine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # /utf-8 : ソースを UTF-8 として読む。日本語コメントや文字列を化けさせないため
    # /W4    : 警告レベルを上げて、初期段階のミスを拾う
    add_compile_options(/utf-8 /W4)
endif()

if(WIN32)
if(WIN32)
    # NOMINMAX    : windows.h の min / max マクロを無効化（std::min と衝突するため）
    # WIN32_LEAN_AND_MEAN : windows.h から使われないヘッダを外して、コンパイルを軽くする
    add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
endif()

# ---- RHI ----
# GPU API の違いを吸収する層。D3D11 / D3D12 のライブラリはこのターゲットだけがリンクする
add_library(RHI STATIC
    src/RHI/RHITypes.hpp
    src/RHI/IRHIBuffer.hpp
    src/RHI/IRHICommandList.hpp
    src/RHI/IRHIDescriptorTable.hpp
    src/RHI/IRHIDevice.hpp
    src/RHI/IRHIFence.hpp
    src/RHI/IRHIPipeline.hpp
    src/RHI/IRHISwapChain.hpp
    src/RHI/IRHITexture.hpp
    src/RHI/RHIBackendCommon.hpp
    src/RHI/RHIFactory.cpp
    src/RHI/RHIUtil.hpp
    src/RHI/RHIUtil.cpp
)
target_include_directories(RHI PUBLIC src)

set(TE_D3D_SOURCES
    src/RHI/D3D12/D3D12Types.hpp
    src/RHI/D3D12/D3D12DescriptorHeap.hpp
    src/RHI/D3D12/D3D12Fence.hpp
    src/RHI/D3D12/D3D12Device.hpp
    src/RHI/D3D12/D3D12Device.cpp
    src/RHI/D3D12/D3D12Resource.hpp
    src/RHI/D3D12/D3D12Resource.cpp
    src/RHI/D3D12/D3D12CommandList.hpp
    src/RHI/D3D12/D3D12CommandList.cpp
    src/RHI/D3D12/D3D12DescriptorTable.hpp
    src/RHI/D3D12/D3D12DescriptorTable.cpp
    src/RHI/D3D12/D3D12Pipeline.hpp
    src/RHI/D3D12/D3D12Pipeline.cpp
    src/RHI/D3D12/D3D12SwapChain.hpp
    src/RHI/D3D12/D3D12SwapChain.cpp

    src/RHI/D3D11/D3D11Types.hpp
    src/RHI/D3D11/D3D11Fence.hpp
    src/RHI/D3D11/D3D11Device.hpp
    src/RHI/D3D11/D3D11Device.cpp
    src/RHI/D3D11/D3D11Resource.hpp
    src/RHI/D3D11/D3D11Resource.cpp
    src/RHI/D3D11/D3D11CommandList.hpp
    src/RHI/D3D11/D3D11CommandList.cpp
    src/RHI/D3D11/D3D11DescriptorTable.hpp
    src/RHI/D3D11/D3D11DescriptorTable.cpp
    src/RHI/D3D11/D3D11Pipeline.hpp
    src/RHI/D3D11/D3D11Pipeline.cpp
    src/RHI/D3D11/D3D11SwapChain.hpp
    src/RHI/D3D11/D3D11SwapChain.cpp

    # Microsoft の D3D12 ヘルパー（ヘッダのみ・MIT）。D3D12 バックエンドの中だけで使う
    src/ThirdParty/d3dx12.h
)

if(WIN32)
    target_sources(RHI PRIVATE ${TE_D3D_SOURCES})
    # d3d11  : D3D11 バックエンド
    # d3d12  : デバイス・コマンドキュー・フェンス
    # dxgi   : ファクトリ・アダプタ列挙・スワップチェイン
    # dxguid : IID_* の GUID 定義
    target_link_libraries(RHI PRIVATE d3d11 d3d12 dxgi dxguid)
endif()

# ---- Vulkan バックエンド ----
set(TE_VULKAN_SOURCES
    src/RHI/Vulkan/VulkanTypes.hpp
    src/RHI/Vulkan/VulkanFence.hpp
    src/RHI/Vulkan/VulkanDevice.hpp
    src/RHI/Vulkan/VulkanDevice.cpp
    src/RHI/Vulkan/VulkanResource.hpp
    src/RHI/Vulkan/VulkanResource.cpp
    src/RHI/Vulkan/VulkanCommandList.hpp
    src/RHI/Vulkan/VulkanCommandList.cpp
    src/RHI/Vulkan/VulkanDescriptorTable.hpp
    src/RHI/Vulkan/VulkanDescriptorTable.cpp
    src/RHI/Vulkan/VulkanPipeline.hpp
    src/RHI/Vulkan/VulkanPipeline.cpp
    src/RHI/Vulkan/VulkanSwapChain.hpp
    src/RHI/Vulkan/VulkanSwapChain.cpp
)

if(ANDROID)
    # Android は Vulkan が必須。NDK の libvulkan をリンクする。dxc は使わない
    set(TE_ENABLE_VULKAN ON)
    target_sources(RHI PRIVATE ${TE_VULKAN_SOURCES})
    target_compile_definitions(RHI PUBLIC TE_RHI_VULKAN=1)
    target_link_libraries(RHI PRIVATE vulkan)
    # RHI のヘッダの SAL 注釈（MSVC 専用）を空にする定義を、全ファイルの先頭で読ませる
    target_compile_options(RHI PUBLIC -include ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/Platform.hpp)
    message(STATUS "Vulkan バックエンド: 有効 (Android NDK)")
else()
    # Vulkan SDK が入っていれば組み込む。なければ D3D11 / D3D12 だけでビルドする（依存を必須にしないため）。
    # dxc は HLSL を SPIR-V にコンパイルするのに使う（SDK に同梱）
    find_package(Vulkan QUIET)
    option(TE_ENABLE_VULKAN "Vulkan バックエンドを組み込む（Vulkan SDK が必要）" ${Vulkan_FOUND})
    if(TE_ENABLE_VULKAN)
        # dxc は SDK の Lib / Bin にある。SDK には Debug 用の dxcompilerd.lib がないが、
        # dxc は C の関数で呼ぶ DLL なので、Release 用の dxcompiler.lib を Debug でもそのまま使える
        get_filename_component(TE_VULKAN_SDK_DIR "${Vulkan_INCLUDE_DIRS}" DIRECTORY)
        find_library(TE_DXC_LIBRARY dxcompiler HINTS "${TE_VULKAN_SDK_DIR}/Lib" NO_DEFAULT_PATH)
        find_file(TE_DXC_DLL dxcompiler.dll HINTS "${TE_VULKAN_SDK_DIR}/Bin" NO_DEFAULT_PATH)
        if(NOT Vulkan_FOUND OR NOT TE_DXC_LIBRARY OR NOT TE_DXC_DLL)
            message(FATAL_ERROR "TE_ENABLE_VULKAN が ON ですが、Vulkan SDK（dxc を含む）が見つかりません")
        endif()
        target_sources(RHI PRIVATE ${TE_VULKAN_SOURCES})
        # TE_RHI_VULKAN は RHI を使う側（Renderer の SPIR-V コンパイル）も見るので PUBLIC にする
        target_compile_definitions(RHI PUBLIC TE_RHI_VULKAN=1)
        target_link_libraries(RHI PRIVATE Vulkan::Vulkan)
        message(STATUS "Vulkan バックエンド: 有効 (${Vulkan_VERSION})")
    else()
        message(STATUS "Vulkan バックエンド: 無効")
    endif()
endif()

# ---- シェーダー ----
# 実行時に ShaderCompiler がファイルから読んでコンパイルする（ホットリロードのため）。
# add_executable に入れるのは IDE に表示するためだけ。HEADER_FILE_ONLY を付けないと、
# VS がビルド時に FXC でコンパイルしようとして「missing entry point」で失敗する
set(TE_SHADER_FILES
    shaders/VulkanBinding.hlsli
    shaders/Common.hlsli
    shaders/Mesh.hlsl
    shaders/Grid.hlsl
    shaders/ImGui.hlsl
    shaders/Present.hlsl
)
set_source_files_properties(${TE_SHADER_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
# 描画は自前（src/UI/ImGuiRenderer が RHI で描く）なので、公式のバックエンドは Win32 だけ使う。
# Android はランタイムだけなので ImGui を含めない
if(NOT ANDROID)
    add_library(imgui STATIC
        external/imgui/imgui.cpp
        external/imgui/imgui_draw.cpp
        external/imgui/imgui_tables.cpp
        external/imgui/imgui_widgets.cpp
        external/imgui/imgui_demo.cpp
        external/imgui/backends/imgui_impl_win32.cpp
        # ギズモ（移動・回転・拡縮の取っ手）。ImGui の上で描くライブラリ（MIT）
        external/ImGuizmo/src/ImGuizmo.cpp
    )
    # SYSTEM: 使う側で imgui のヘッダの警告を /W4 の対象にしない
    target_include_directories(imgui SYSTEM PUBLIC external/imgui external/ImGuizmo/src)
    # ImGuizmo の中の符号の変換（int → ImGuiID）の警告。外部のコードなので、そのファイルだけ止める
    if(MSVC)
        set_source_files_properties(external/ImGuizmo/src/ImGuizmo.cpp PROPERTIES COMPILE_OPTIONS "/wd4245")
    endif()
    # 古い API を使っていたらコンパイルエラーにして気付けるようにする
    target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
endif()


# ---- 本体 ----
set(TE_ENGINE_SOURCES
    src/main.cpp
    src/Reflection.hpp
    src/Core/FixedString.hpp
    src/Scene/Name.hpp
    src/Engine/EngineTypes.hpp
    src/Engine/EngineTypes.cpp

    src/Core/Defines.hpp
    src/Core/Log.hpp
    src/Core/Timer.hpp
    src/Core/Platform.hpp

    src/Platform/Window.hpp
    src/Platform/Window.cpp
    src/Platform/Window_Android.cpp
    src/Platform/Input.hpp
    src/Platform/Input.cpp
    src/Platform/Paths.hpp

    src/Renderer/ShaderCompiler.hpp
    src/Renderer/ShaderCompiler.cpp
    src/Renderer/ShaderWatcher.hpp
    src/Renderer/MeshRenderer.hpp
    src/Renderer/Renderer.hpp
    src/Renderer/Renderer.cpp
    src/Renderer/ModelUpload.hpp
    src/Renderer/ModelUpload.cpp
    src/Renderer/RenderSystem.cpp
    src/Renderer/RenderSystem.hpp

    src/Engine/Engine.hpp
    src/Engine/Engine.cpp

    src/Math/Math.hpp
    src/Math/Mathf.hpp
    src/Math/Vector2.hpp
    src/Math/Vector3.hpp
    src/Math/Vector4.hpp
    src/Math/VectorInt.hpp
    src/Math/Quaternion.hpp
    src/Math/Matrix4x4.hpp
    src/Math/Color.hpp
    src/Math/Rect.hpp
    src/Math/Ray.hpp
    src/Math/Plane.hpp
    src/Math/Bounds.hpp
    src/Math/BoundingSphere.hpp
    src/Math/Random.hpp
    src/Math/Easing.hpp
    src/Math/Intersection.hpp
    src/Math/Frustum.hpp
    src/Math/Half.hpp

    src/ECS/Entity.hpp
    src/ECS/ComponentType.hpp
    src/ECS/ArcheType.hpp
    src/ECS/World.hpp
    src/ECS/ECS.hpp

    src/Game/Spin.hpp

    src/Scene/Transform.hpp
    src/Scene/TransformSystem.hpp
    src/Scene/Camera.hpp
    src/Scene/OrbitCamera.hpp

    src/Asset/ModelData.hpp
    src/Asset/ModelImporter.hpp
    src/Asset/ModelImporter.cpp

    src/UI/ImGuiLayer.hpp
    src/UI/ImGuiLayer.cpp
    src/UI/ImGuiRenderer.hpp
    src/UI/ImGuiRenderer.cpp

    src/Editor/Editor.hpp
    src/Editor/Editor.cpp
    src/Editor/ConsolePanel.hpp
    src/Editor/ConsolePanel.cpp

    ${TE_SHADER_FILES}
)

# Android のアプリ本体は exe ではなく、APK に入れて読み込む .so として作る
if(ANDROID)
    # Android はランタイムだけ。Editor と ImGui は含めない
    list(FILTER TE_ENGINE_SOURCES EXCLUDE REGEX "^src/(UI|Editor)/")
    list(APPEND TE_ENGINE_SOURCES
        src/Platform/Window_Android.cpp
        ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
    )
    add_library(TentativeEngine SHARED ${TE_ENGINE_SOURCES})
else()
    add_executable(TentativeEngine WIN32 ${TE_ENGINE_SOURCES})
endif()

target_include_directories(TentativeEngine PRIVATE src)

target_link_libraries(TentativeEngine PRIVATE RHI)

if(ANDROID)
    # assimp : モデル読み込み（Windows のビルド済みは使えないのでソースからビルドする）
    # stb    : 画像のデコード（ヘッダのみ）
    include(FetchContent)
    set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
    set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(assimp
        GIT_REPOSITORY https://github.com/assimp/assimp.git
        GIT_TAG v5.4.3)
    FetchContent_Declare(stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG master)
    FetchContent_MakeAvailable(assimp stb)
    # assimp が Debug の接尾辞 d を全体に付ける。名前が変わると AndroidManifest の lib_name で読めなくなる
    set_target_properties(TentativeEngine PROPERTIES DEBUG_POSTFIX "")

    target_include_directories(TentativeEngine PRIVATE
        ${ANDROID_NDK}/sources/android/native_app_glue
        ${stb_SOURCE_DIR})
    # glue の入口をリンカに消させない
    target_link_options(TentativeEngine PRIVATE -u ANativeActivity_onCreate)
    # log     : __android_log_write（Logcat への出力）
    # android : ANativeWindow / AAssetManager
    target_link_libraries(TentativeEngine PRIVATE assimp log android)
else()
    target_link_libraries(TentativeEngine PRIVATE imgui)

    # d3dcompiler : 実行時の HLSL コンパイル（ShaderCompiler.cpp だけが使う）
    # xinput      : ゲームパッド（Input.cpp だけが使う）
    target_link_libraries(TentativeEngine PRIVATE d3dcompiler xinput)

    if(TE_ENABLE_VULKAN)
        # Vulkan 用に HLSL を SPIR-V へコンパイルする dxc（ShaderCompiler.cpp だけが使う）
        target_include_directories(TentativeEngine PRIVATE ${Vulkan_INCLUDE_DIRS})
        target_link_libraries(TentativeEngine PRIVATE "${TE_DXC_LIBRARY}")
        # dxc は実行時に dxcompiler.dll を読むので、exe の隣にコピーしておく（PATH に頼らない）
        add_custom_command(TARGET TentativeEngine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TE_DXC_DLL}" $<TARGET_FILE_DIR:TentativeEngine>
        )
    endif()

    # ---- 外部ライブラリ（ビルド済み。ModelViewer の external/ をコピーしたもの）----
    # assimp     : FBX / glTF / OBJ などのモデル読み込み（DLL）
    # DirectXTex : 画像のデコードとミップマップ生成（静的ライブラリ）
    # Debug 以外（Release / RelWithDebInfo / MinSizeRel）は Release 版を使う。
    # CRT（/MDd と /MD）を混ぜるとリンクエラーや実行時の不具合になるので、構成で選び分ける
    set(TE_EXTERNAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external)
    set(TE_EXTERNAL_CONFIG $<IF:$<CONFIG:Debug>,Debug,Release>)
    set(TE_ASSIMP_NAME $<IF:$<CONFIG:Debug>,assimp-vc142-mtd,assimp-vc142-mt>)

    # SYSTEM: 外部ヘッダの警告は /W4 の対象にしない
    target_include_directories(TentativeEngine SYSTEM PRIVATE
        ${TE_EXTERNAL_DIR}/assimp/include
        ${TE_EXTERNAL_DIR}/DirectXTex/include
    )
    # ole32 : WIC（DirectXTex の png / jpg 読み込み）が使う COM
    target_link_libraries(TentativeEngine PRIVATE
        ${TE_EXTERNAL_DIR}/assimp/lib/${TE_EXTERNAL_CONFIG}/${TE_ASSIMP_NAME}.lib
        ${TE_EXTERNAL_DIR}/DirectXTex/lib/${TE_EXTERNAL_CONFIG}/DirectXTex.lib
        ole32
    )
    # assimp は DLL なので exe の隣に置く（PATH に頼らない）
    add_custom_command(TARGET TentativeEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${TE_EXTERNAL_DIR}/assimp/lib/${TE_EXTERNAL_CONFIG}/${TE_ASSIMP_NAME}.dll
            $<TARGET_FILE_DIR:TentativeEngine>
    )

    # ---- 配布用のコピー（Debug 以外）----
    # shaders と assets を exe の横に置き、Release のフォルダだけ持っていけば別の PC で動くようにする。
    # Debug はコピーしない。コピーがあると exe から探したときにそちらが先に見つかり、
    # ソースツリーのシェーダーを直してもホットリロードされなくなるため
    add_custom_command(TARGET TentativeEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E $<IF:$<CONFIG:Debug>,true,copy_directory>
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:TentativeEngine>/shaders
        COMMAND ${CMAKE_COMMAND} -E $<IF:$<CONFIG:Debug>,true,copy_directory>
            ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:TentativeEngine>/assets
    )
endif()

# ---- Math ----
# ヘッダだけのライブラリ。何にも依存しない。使う側は target_link_libraries(... Math)
add_library(Math INTERFACE)
target_include_directories(Math INTERFACE src)

# ---- テスト（ウィンドウも GPU も使わないのでコンソールで動く）----
enable_testing()

add_executable(MathTests tests/MathTests.cpp)
target_link_libraries(MathTests PRIVATE Math)
add_test(NAME MathTests COMMAND MathTests)

if(WIN32)
    # DirectXMath（Windows SDK に入っている）との答え合わせ。本体は DirectXMath に依存しない
    add_executable(MathCompareTests tests/MathCompareTests.cpp)
    target_link_libraries(MathCompareTests PRIVATE Math)
    add_test(NAME MathCompareTests COMMAND MathCompareTests)
endif()
