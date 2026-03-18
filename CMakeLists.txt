cmake_minimum_required(VERSION 3.26)

project(My2026Graphics LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "My2026Graphics 는 Windows 환경에서만 빌드할 수 있습니다.")
endif()

if(MSVC)
    # 한글 깨짐 방지 및 UTF-8 소스 처리 옵션
    add_compile_options(/utf-8)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

set(GraphicsRoot ${CMAKE_CURRENT_SOURCE_DIR})
set(ExternalIncludeDir ${GraphicsRoot}/External/Include)
set(ExternalDirectXIncludeDir ${ExternalIncludeDir}/DirectX)
set(ExternalLibDirDebug ${GraphicsRoot}/External/lib/debug)
set(ExternalLibDirRelease ${GraphicsRoot}/External/lib/release)

add_library(Utility STATIC
    Utility/ErrorHandler.cpp
    Utility/StdOutput.cpp
)

target_include_directories(Utility PUBLIC
    ${GraphicsRoot}
    ${ExternalIncludeDir}
    ${ExternalDirectXIncludeDir}
)

target_compile_definitions(Utility PRIVATE
    UNICODE
    _UNICODE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

add_library(Arche STATIC
    Arche/World.cpp
    Arche/ArcheType.cpp
    Arche/Memory.cpp
    Arche/TypeSystem.cpp
)

target_include_directories(Arche PUBLIC
    ${GraphicsRoot}
)

target_link_libraries(Arche PUBLIC Utility)

target_compile_definitions(Arche PRIVATE
    _LIB
    UNICODE
    _UNICODE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

add_library(Asset STATIC
    Asset/AssetBinaryReader.cpp
    Asset/AssetBinaryWriter.cpp
    Asset/cgltf.c
    Asset/Common.cpp
    Asset/DataList.cpp
    Asset/FbxAssetImporter.cpp
    Asset/GltfAssetImporter.cpp
    Asset/GltfAssetLoader.cpp
    Asset/GltfMaterialVisitor.cpp
    Asset/GltfMeshHierarchyBuilder.cpp
    Asset/MaterialVisitor.cpp
    Asset/MeshHierarchyBuilder.cpp
    Asset/MaterialGroupJsonSerializer.cpp
    Asset/ModelResult.cpp
    Asset/NumericTypes.cpp
    Asset/ufbx.c
    Asset/UfbxAssetLoader.cpp
)

target_include_directories(Asset PUBLIC
    ${GraphicsRoot}
    ${ExternalIncludeDir}
)

target_link_libraries(Asset PUBLIC Utility)

target_compile_definitions(Asset PRIVATE
    _LIB
    UNICODE
    _UNICODE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

add_library(Game STATIC
    Game/Base/Shader.cpp
    Game/Base/RootSignature.cpp
    Game/Base/Pipeline.cpp
    Game/Model/AssetRegistry.cpp
    Game/Model/AssetRegistryBackEnd.cpp
    Game/Model/Model.cpp
    Game/Model/PrimitiveModelFactory.cpp
    Game/Scene/Scene.cpp
    Game/Scene/SceneYamlSerializer.cpp
    Game/Scene/System.cpp
    Game/Scene/SystemSceduler.cpp
    Game/Scene/Components/Transform.cpp
    Game/Scene/Components/Name.cpp
    Game/Scene/Components/Material.cpp
    Game/Scene/Components/EntityHierarchy.cpp
    Game/Scene/Components/Tags.cpp
    Game/Scene/Components/Intents/CameraIntent.cpp
    Game/Scene/Components/StaticMeshRenderer.cpp
    Game/Scene/Components/BoundingBox.cpp
    Game/Scene/Components/Camera.cpp
    Game/Scene/Components/Frustum.cpp
    Game/Scene/Components/PrefabInstance.cpp
    Game/Scene/Components/ComponentInspection.cpp
    Game/Scene/Systems/CameraInputSystem.cpp
    Game/Scene/Systems/CameraRenderSystem.cpp
    Game/Scene/Systems/PickingSystem.cpp
    Game/Scene/Systems/StaticRenderSystem.cpp
)

target_include_directories(Game PUBLIC
    ${GraphicsRoot}
    ${ExternalIncludeDir}
)

target_link_libraries(Game PUBLIC Utility)

target_compile_definitions(Game PRIVATE
    _LIB
    UNICODE
    _UNICODE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

add_executable(2026_Graphics WIN32
    2026_Graphics.cpp
    2026_Graphics.rc
    Core/Config.cpp
    Core/Event/Event.cpp
    Core/Event/EventQueue.cpp
    Core/Event/FileDropEvent.cpp
    Core/DX/AllocationHandle.cpp
    Core/DX/ComputeQueue.cpp
    Core/DX/CopyQueue.cpp
    Core/DX/DesciptorHeap.cpp
    Core/DX/DrawCallDispatcher.cpp
    Core/DX/DrawCallResourceManager.cpp
    Core/DX/MaterialResourceManager.cpp
    Core/DX/DirectQueue.cpp
    Core/DX/FrameSync.cpp
    Core/DX/GraphicsAllocator.cpp
    Core/DX/GraphicsVector.cpp
    Core/DX/Texture.cpp
)

set_target_properties(2026_Graphics PROPERTIES OUTPUT_NAME "2026_Graphics")

target_include_directories(2026_Graphics PRIVATE
    ${GraphicsRoot}
    ${ExternalIncludeDir}
    ${ExternalDirectXIncludeDir}
)

target_link_directories(2026_Graphics PRIVATE
    $<$<CONFIG:Debug>:${ExternalLibDirDebug}>
    $<$<CONFIG:Release>:${ExternalLibDirRelease}>
)

target_link_libraries(2026_Graphics PRIVATE
    Arche
    Asset
    Game
    Utility
    d3d12
    dxgi
    dxguid
    D3DCompiler
)

target_compile_definitions(2026_Graphics PRIVATE
    _WINDOWS
    UNICODE
    _UNICODE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

add_custom_command(TARGET 2026_Graphics POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GraphicsRoot}/Core/Config.prop $<TARGET_FILE_DIR:2026_Graphics>/Config.prop
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GraphicsRoot}/dxcompiler.dll $<TARGET_FILE_DIR:2026_Graphics>/dxcompiler.dll
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${GraphicsRoot}/D3D $<TARGET_FILE_DIR:2026_Graphics>/D3D
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${GraphicsRoot}/Shader $<TARGET_FILE_DIR:2026_Graphics>/Shader
)

add_executable(AssetZIP
    AssetZIP/AssetZIP.cpp
)

target_include_directories(AssetZIP PRIVATE
    ${GraphicsRoot}
    ${ExternalIncludeDir}
)

target_link_libraries(AssetZIP PRIVATE
    Asset
    Utility
)

target_compile_definitions(AssetZIP PRIVATE
    UNICODE
    _UNICODE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)
