cmake_minimum_required(VERSION 3.20)
project(TentativeEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # /utf-8 : ソースを UTF-8 として読む。日本語コメントや文字列を化けさせないため
    # /W4    : 警告レベルを上げて、初期段階のミスを拾う
    add_compile_options(/utf-8 /W4)
endif()

# NOMINMAX    : windows.h の min / max マクロを無効化（std::min と衝突するため）
# WIN32_LEAN_AND_MEAN : windows.h から使われないヘッダを外して、コンパイルを軽くする
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

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
target_include_directories(RHI PUBLIC src)

# d3d11  : D3D11 バックエンド
# d3d12  : デバイス・コマンドキュー・フェンス
# dxgi   : ファクトリ・アダプタ列挙・スワップチェイン
# dxguid : IID_* の GUID 定義
target_link_libraries(RHI PRIVATE d3d11 d3d12 dxgi dxguid)

# ---- Vulkan バックエンド（任意）----
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
    target_sources(RHI PRIVATE
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
    # TE_RHI_VULKAN は RHI を使う側（Renderer の SPIR-V コンパイル）も見るので PUBLIC にする
    target_compile_definitions(RHI PUBLIC TE_RHI_VULKAN=1)
    target_link_libraries(RHI PRIVATE Vulkan::Vulkan)
    message(STATUS "Vulkan バックエンド: 有効 (${Vulkan_VERSION})")
else()
    message(STATUS "Vulkan バックエンド: 無効")
endif()

# ---- 本体 ----
add_executable(TentativeEngine WIN32
    src/main.cpp

    src/Core/Defines.hpp
    src/Core/Log.hpp
    src/Core/Timer.hpp

    src/Platform/Window.hpp
    src/Platform/Window.cpp

    src/Renderer/ShaderCompiler.hpp
    src/Renderer/ShaderCompiler.cpp
    src/Renderer/MeshRenderer.hpp
    src/Renderer/Renderer.hpp
    src/Renderer/Renderer.cpp
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

 
)

target_include_directories(TentativeEngine PRIVATE src)

# d3dcompiler : 実行時の HLSL コンパイル（ShaderCompiler.cpp だけが使う）
target_link_libraries(TentativeEngine PRIVATE RHI d3dcompiler)

if(TE_ENABLE_VULKAN)
    # Vulkan 用に HLSL を SPIR-V へコンパイルする dxc（ShaderCompiler.cpp だけが使う）
    target_include_directories(TentativeEngine PRIVATE ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(TentativeEngine PRIVATE "${TE_DXC_LIBRARY}")
    # dxc は実行時に dxcompiler.dll を読むので、exe の隣にコピーしておく（PATH に頼らない）
    add_custom_command(TARGET TentativeEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TE_DXC_DLL}" $<TARGET_FILE_DIR:TentativeEngine>
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

# DirectXMath（Windows SDK に入っている）との答え合わせ。本体は DirectXMath に依存しない
add_executable(MathCompareTests tests/MathCompareTests.cpp)
target_link_libraries(MathCompareTests PRIVATE Math)
add_test(NAME MathCompareTests COMMAND MathCompareTests)
