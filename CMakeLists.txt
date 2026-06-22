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
set(ExternalRapidYmlIncludeDir ${ExternalIncludeDir}/rapidyml)
set(ExternalLuaIncludeDir ${ExternalIncludeDir}/lua)
set(ExternalLibDirDebug ${GraphicsRoot}/External/lib/debug)
set(ExternalLibDirRelease ${GraphicsRoot}/External/lib/release)

add_library(Utility STATIC
    Utility/ErrorHandler.cpp
    Utility/MathValidation.cpp
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
    Asset/AnimationBinaryReader.cpp
    Asset/AnimationClipBinaryWriter.cpp
    Asset/AnimationClipResult.cpp
    Asset/AssimpAnimationClipImporter.cpp
    Asset/AssetBinaryReader.cpp
    Asset/AssetBinaryWriter.cpp
    Asset/Common.cpp
    Asset/DataList.cpp
    Asset/AssimpAssetImporter.cpp
    Asset/MaterialGroupJsonSerializer.cpp
    Asset/ModelResult.cpp
)

target_include_directories(Asset PUBLIC
    ${GraphicsRoot}
    ${ExternalIncludeDir}
)

target_link_libraries(Asset PUBLIC Utility $<$<CONFIG:Debug>:assimp-vc143-mtd> $<$<NOT:$<CONFIG:Debug>>:assimp-vc143-mt>)

target_compile_definitions(Asset PRIVATE
    _LIB
    UNICODE
    _UNICODE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

add_library(PhysicsLib STATIC
    PhysicsLib/Common.cpp
    PhysicsLib/Actors/CollisionSolver/PhysicsDynamicCollisionSolver.cpp
    PhysicsLib/Actors/CollisionSolver/PhysicsKinematicCollisionSolver.cpp
    PhysicsLib/Actors/CollisionSolver/PhysicsStaticCollisionSolver.cpp
    PhysicsLib/Actors/ConstraintSolver/PhysicsNoConstraintSolver.cpp
    PhysicsLib/Actors/Integrater/PhysicsDynamicIntegrater.cpp
    PhysicsLib/Actors/Integrater/PhysicsKinematicIntegrater.cpp
    PhysicsLib/Actors/Integrater/PhysicsStaticIntegrater.cpp
    PhysicsLib/Actors/PhysicsActorBase.cpp
    PhysicsLib/Actors/PhysicsTerrainActor.cpp
    PhysicsLib/Actors/SolverType/PhysicsSolverType.cpp
    PhysicsLib/Simulation/Mediator/IPhysicsWorldMediator.cpp
    PhysicsLib/Simulation/Kinematic/PhysicsKinematicActorSimulator.cpp
    PhysicsLib/Simulation/Kinematic/PhysicsKinematicSceneSimulator.cpp
    PhysicsLib/Simulation/Repository/IPhysicsActorRepository.cpp
    PhysicsLib/Simulation/Repository/PhysicsActorRepository.cpp
    PhysicsLib/Simulation/SpatialQuery/BruteForcePhysicsSpatialQuery.cpp
    PhysicsLib/Simulation/SpatialQuery/IPhysicsSpatialQuery.cpp
    PhysicsLib/Runtime/PhysicsRuntime.cpp
    PhysicsLib/World/PhysicsWorld.cpp
)

target_include_directories(PhysicsLib PUBLIC
    ${GraphicsRoot}
    ${ExternalIncludeDir}
)

target_link_directories(PhysicsLib PUBLIC
    $<$<CONFIG:Debug>:${ExternalLibDirDebug}>
    $<$<CONFIG:Release>:${ExternalLibDirRelease}>
)

target_compile_definitions(PhysicsLib PRIVATE
    _LIB
    UNICODE
    _UNICODE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

if(MSVC)
    target_compile_options(PhysicsLib PRIVATE /bigobj)
endif()

add_library(RenderContract STATIC
    RenderContract/Frame/ShadowMappingParameter.cpp
    RenderContract/Future/Future.cpp
    RenderContract/Gather/RenderGatherResult.cpp
    RenderContract/Gather/RenderGatherResultMerger.cpp
    RenderContract/Geometry/DebugGeometryContext.cpp
    RenderContract/Writer/FrameRenderWriter.cpp
    RenderContract/Writer/EnvironmentRenderWriter.cpp
    RenderContract/Writer/TerrainRenderWriter.cpp
)

target_include_directories(RenderContract PUBLIC
    ${GraphicsRoot}
    ${ExternalIncludeDir}
    ${ExternalDirectXIncludeDir}
    ${ExternalRapidYmlIncludeDir}
    ${ExternalLuaIncludeDir}
)

target_link_directories(RenderContract PUBLIC
    $<$<CONFIG:Debug>:${ExternalLibDirDebug}>
    $<$<CONFIG:Release>:${ExternalLibDirRelease}>
)

target_compile_definitions(RenderContract PRIVATE
    _LIB
    NOMINMAX
    UNICODE
    _UNICODE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

add_library(Terrain STATIC
    Terrain/HeightMapLoader.cpp
    Terrain/TerrainHeightFieldFactory.cpp
    Terrain/TerrainManager.cpp
    Terrain/TerrainMeshBuilder.cpp
    Terrain/TerrainProceduralHeightFieldConfigLoader.cpp
    Terrain/TerrainProceduralHeightFieldGenerator.cpp
    Terrain/TerrainRenderDataBuilder.cpp
    Terrain/TerrainSplatMapGenerator.cpp
    Terrain/TerrainTiledMeshBuilder.cpp
)

target_include_directories(Terrain PUBLIC
    ${GraphicsRoot}
    ${ExternalIncludeDir}
    ${ExternalDirectXIncludeDir}
    ${ExternalRapidYmlIncludeDir}
)

target_link_directories(Terrain PUBLIC
    $<$<CONFIG:Debug>:${ExternalLibDirDebug}>
    $<$<CONFIG:Release>:${ExternalLibDirRelease}>
)

target_link_libraries(Terrain PUBLIC Asset RenderContract Utility DirectXTK12)

target_compile_definitions(Terrain PRIVATE
    _LIB
    NOMINMAX
    UNICODE
    _UNICODE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

target_link_libraries(PhysicsLib PUBLIC Terrain)

if(MSVC)
    target_compile_options(Terrain PRIVATE /bigobj)
endif()

add_library(Game STATIC
    Game/Asset/AnimationGraphAsset.cpp
    Game/Base/Shader.cpp
    Game/Base/RootSignature.cpp
    Game/Base/Pipeline.cpp
    Game/Base/Input.cpp
    Game/Environment/EnvironmentObjectRenderContext.cpp
    Game/Environment/EnvironmentObjectRenderDataBuilder.cpp
    Game/Environment/EnvironmentObjectTypes.cpp
    Game/Model/AssetRegistry.cpp
    Game/Model/AssetRegistryBackEnd.cpp
    Game/Model/Model.cpp
    Game/Model/PrimitiveModelFactory.cpp
    Game/Model/TerrainRenderResource.cpp
    Game/Scene/Legacy/Scene.cpp
    Game/Scene/Base/SceneEntityFactory.cpp
    Game/Scene/Base/ScenePhysicsRuntimeCoordinator.cpp
    Game/Scene/Base/SceneWorldSnapshot.cpp
    Game/Scene/Legacy/SceneYamlSerializer.cpp
    Game/Scene/SceneYaml/SceneYamlCommon.cpp
    Game/Scene/SceneYaml/SceneYamlComponentRegistry.cpp
    Game/Scene/SceneYaml/Components/SceneYamlAnimationComponent.cpp
    Game/Scene/SceneYaml/Components/SceneYamlBasicComponents.cpp
    Game/Scene/SceneYaml/Components/SceneYamlCameraComponent.cpp
    Game/Scene/SceneYaml/Components/SceneYamlNameComponent.cpp
    Game/Scene/SceneYaml/Components/SceneYamlPhysicsComponent.cpp
    Game/Scene/SceneYaml/Components/SceneYamlTerrainComponent.cpp
    Game/Scene/SceneYaml/Components/SceneYamlTransformComponent.cpp
    Game/Scene/SceneYaml/SceneYamlDeserializer.cpp
    Game/Scene/SceneYaml/SceneYamlTerrain.cpp
    Game/Scene/SceneYaml/SceneYamlWriter.cpp
    Game/Scene/Legacy/SystemSceduler.cpp
    Game/Scene/Legacy/ParallelSystemBase.cpp
    Game/Scene/Base/Context.cpp
    Game/Scene/Base/Types.cpp
    Game/Scene/Base/SceneWorkUnit.cpp
    Game/Scene/Base/Executor.cpp
    Game/Scene/Base/SystemBindingResult.cpp
    Game/Scene/Base/SystemRegistry.cpp
    Game/Scene/Base/Scene.cpp
    Game/Scene/Base/PipelineYAMLSerializer.cpp
    Game/Scene/Base/PipelineYAMLMetadata.cpp
    Game/Scene/Base/PipelineYAMLDeserializer.cpp
    Game/Scene/Base/SceneWorkUnitBuilder.cpp
    Game/Scene/Systems/AnimationGraphSystem.cpp
    Game/Scene/Systems/AnimateSystem.cpp
    Game/Scene/Systems/FootIKSystem.cpp
    Game/Scene/Systems/TransformWorldSystem.cpp
    Game/Scene/Systems/StaticRenderSystem.cpp
    Game/Scene/Systems/TerrainRenderSystem.cpp
    Game/Scene/Systems/SkinnedRenderSystem.cpp
    Game/Scene/Systems/RegisterDefaultSystems.cpp
    Game/Scene/Components/Transform.cpp
    Game/Scene/Components/ComponentText.cpp
    Game/Scene/Components/Name.cpp
    Game/Scene/Components/Material.cpp
    Game/Scene/Components/EntityHierarchy.cpp
    Game/Scene/Components/Tags.cpp
    Game/Scene/Components/StaticMeshRenderer.cpp
    Game/Scene/Components/TerrainRenderer.cpp
    Game/Scene/Components/SkinnedMeshRenderer.cpp
    Game/Scene/Components/Culling.cpp
    Game/Scene/Components/BoundingBox.cpp
    Game/Scene/Components/Bone.cpp
    Game/Scene/Components/BoneSkinReference.cpp
    Game/Scene/Components/Camera.cpp
    Game/Scene/Components/DirectionalLight.cpp
    Game/Scene/Components/SkySphere.cpp
    Game/Scene/Components/Frustum.cpp
    Game/Scene/Components/PrefabInstance.cpp
    Game/Scene/Components/ScriptComponent.cpp
    Game/Scene/Components/RuntimeVariableTable.cpp
    Game/Scene/Components/Animator.cpp
    Game/Scene/Components/AnimatorGraphPlayer.cpp
    Game/Scene/Components/ComponentInspection.cpp
    Game/Scene/Components/FootIKRig.cpp
    Game/Scene/Components/FootIKRuntime.cpp
    Game/Scene/Components/PhysicsActor.cpp
    Game/Scene/Systems/CameraRenderSystem.cpp
    Game/Scene/Legacy/Systems/AnimationGraphSystem.cpp
    Game/Scene/Legacy/Systems/AnimateSystem.cpp
    Game/Scene/IK/FootIKSolver.cpp
    Game/Scene/IK/FabrikFootIKSolver.cpp
    Game/Scene/IK/FootIKAlgorithms.cpp
    Game/Scene/Legacy/Systems/FootIKSystem.cpp
    Game/Scene/Systems/PhysicsActorUpdateSystem.cpp
    Game/Scene/Systems/ProceduralFoliageSystem.cpp
    Game/Scene/Legacy/Systems/TransformWorldSystem.cpp
    Game/Scene/Legacy/Systems/SkinnedMeshPrepareSystem.cpp
    Game/Scene/Systems/ShadowMappingParameterSystem.cpp
    Game/Scene/Legacy/Systems/StaticRenderSystem.cpp
    Game/Scene/Systems/TerrainStreamingSystem.cpp
    Game/Scene/Legacy/Systems/TerrainRenderSystem.cpp
    Game/Scene/Legacy/Systems/SkinnedMeshRenderSystem.cpp
    Script/Core/LuaScriptFramework.cpp
)

target_include_directories(Game PUBLIC
    ${GraphicsRoot}
    ${ExternalIncludeDir}
    ${ExternalRapidYmlIncludeDir}
    ${ExternalLuaIncludeDir}
)

target_link_directories(Game PUBLIC
    $<$<CONFIG:Debug>:${ExternalLibDirDebug}>
    $<$<CONFIG:Release>:${ExternalLibDirRelease}>
)

target_link_libraries(Game PUBLIC RenderContract Terrain Utility PhysicsLib lua54 DirectXTK12)

target_compile_definitions(Game PRIVATE
    _LIB
    NOMINMAX
    UNICODE
    _UNICODE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

if(MSVC)
    target_compile_options(Game PRIVATE /bigobj)
endif()

add_executable(2026_Graphics WIN32
    2026_Graphics.cpp
    2026_Graphics.rc
    Core/Config.cpp
    Core/Event/EventQueue.cpp
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
    External/Include/ImGui/imgui.cpp
    External/Include/ImGui/imgui_draw.cpp
    External/Include/ImGui/imgui_impl_dx12.cpp
    External/Include/ImGui/imgui_impl_win32.cpp
    External/Include/ImGui/imgui_tables.cpp
    External/Include/ImGui/imgui_widgets.cpp
    Widget/Console.cpp
    Widget/PerformanceProvider.cpp
    Widget/PerformanceWidgets.cpp
    Widget/SceneHierarchyWidget.cpp
    Widget/WidgetCore.cpp
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
    GRAPHICS_CMAKE_BUILD
    _WINDOWS
    UNICODE
    _UNICODE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

add_custom_command(TARGET 2026_Graphics POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GraphicsRoot}/Config.prop $<TARGET_FILE_DIR:2026_Graphics>/Config.prop
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GraphicsRoot}/dxcompiler.dll $<TARGET_FILE_DIR:2026_Graphics>/dxcompiler.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GraphicsRoot}/c4core.dll $<TARGET_FILE_DIR:2026_Graphics>/c4core.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GraphicsRoot}/ryml.dll $<TARGET_FILE_DIR:2026_Graphics>/ryml.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GraphicsRoot}/lua54.dll $<TARGET_FILE_DIR:2026_Graphics>/lua54.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GraphicsRoot}/$<$<CONFIG:Debug>:assimp-vc143-mtd.dll>$<$<NOT:$<CONFIG:Debug>>:assimp-vc143-mt.dll> $<TARGET_FILE_DIR:2026_Graphics>/$<$<CONFIG:Debug>:assimp-vc143-mtd.dll>$<$<NOT:$<CONFIG:Debug>>:assimp-vc143-mt.dll>
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${GraphicsRoot}/D3D $<TARGET_FILE_DIR:2026_Graphics>/D3D
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${GraphicsRoot}/Resources $<TARGET_FILE_DIR:2026_Graphics>/Resources
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${GraphicsRoot}/Script $<TARGET_FILE_DIR:2026_Graphics>/Script
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${GraphicsRoot}/Shader $<TARGET_FILE_DIR:2026_Graphics>/Shader
)

add_executable(AssetZIP
    AssetZIP/AssetZIP.cpp
)

target_include_directories(AssetZIP PRIVATE
    ${GraphicsRoot}
    ${ExternalIncludeDir}
)

target_link_directories(AssetZIP PRIVATE
    $<$<CONFIG:Debug>:${ExternalLibDirDebug}>
    $<$<CONFIG:Release>:${ExternalLibDirRelease}>
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
