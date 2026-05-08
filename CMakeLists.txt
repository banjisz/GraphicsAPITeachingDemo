cmake_minimum_required(VERSION 3.20)

if(APPLE)
    project(GraphicsAPITeachingDemo LANGUAGES C CXX OBJC OBJCXX)
else()
    project(GraphicsAPITeachingDemo LANGUAGES C CXX)
endif()


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(APP_NAME GraphicsAPITeachingDemo)
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(DIRECTX_SRC_DIR ${SRC_DIR}/directx)
set(METAL_SRC_DIR ${SRC_DIR}/metal)
set(OPENGL_SRC_DIR ${SRC_DIR}/opengl)
set(VULKAN_SRC_DIR ${SRC_DIR}/vulkan)
set(SHARED_SRC_DIR ${SRC_DIR}/shared)

if(APPLE)
    set(SOURCES
        ${METAL_SRC_DIR}/main.mm
        ${METAL_SRC_DIR}/AppDelegate.mm
        ${METAL_SRC_DIR}/Renderer.mm
        ${OPENGL_SRC_DIR}/OpenGLRenderer.mm
    )

    set(HEADERS
        ${METAL_SRC_DIR}/AppDelegate.h
        ${METAL_SRC_DIR}/Renderer.h
        ${OPENGL_SRC_DIR}/OpenGLRenderer.h
    )

    set(METAL_SHADER
        shaders/triangle.metal
    )

    add_executable(${APP_NAME} MACOSX_BUNDLE
        ${SOURCES}
        ${HEADERS}
        ${METAL_SHADER}
    )

    target_compile_options(${APP_NAME} PRIVATE
        "-fobjc-arc"
    )

    target_compile_definitions(${APP_NAME} PRIVATE
        GL_SILENCE_DEPRECATION=1
    )

    target_include_directories(${APP_NAME} PRIVATE
        ${METAL_SRC_DIR}
        ${OPENGL_SRC_DIR}
    )

    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(METALFX_FRAMEWORK MetalFX REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)

    target_link_libraries(${APP_NAME} PRIVATE
        ${COCOA_FRAMEWORK}
        ${METAL_FRAMEWORK}
        ${METALFX_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK}
        ${OPENGL_FRAMEWORK}
    )

    set_source_files_properties(${METAL_SHADER} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
    )

    set_target_properties(${APP_NAME} PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.example.graphicsapiteachingdemo"
        MACOSX_DEPLOYMENT_TARGET "14.0"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.example.graphicsapiteachingdemo"
    )
elseif(WIN32)
    set(DIRECTX_SOURCES
        ${DIRECTX_SRC_DIR}/DXTeachingMain.cpp
        ${DIRECTX_SRC_DIR}/DX11Renderer.cpp
        ${DIRECTX_SRC_DIR}/DX12Renderer.cpp
        ${OPENGL_SRC_DIR}/OpenGLRendererWin.cpp
        ${VULKAN_SRC_DIR}/VulkanRenderer.cpp
    )

    set(DIRECTX_HEADERS
        ${DIRECTX_SRC_DIR}/DX11Renderer.h
        ${DIRECTX_SRC_DIR}/DX12Renderer.h
        ${OPENGL_SRC_DIR}/OpenGLRendererWin.h
        ${VULKAN_SRC_DIR}/VulkanRenderer.h
        ${SHARED_SRC_DIR}/DiagnosticsLog.h
        ${SHARED_SRC_DIR}/DXTeachingShared.h
        ${SHARED_SRC_DIR}/IRendererBackend.h
    )

    set(VULKAN_SHADER_DIR ${VULKAN_SRC_DIR}/shaders)
    set(VULKAN_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
    set(VULKAN_VERTEX_SHADER ${VULKAN_SHADER_DIR}/teaching.vert)
    set(VULKAN_FRAGMENT_SHADER ${VULKAN_SHADER_DIR}/teaching.frag)
    set(VULKAN_VERTEX_SPV ${VULKAN_SHADER_OUTPUT_DIR}/teaching.vert.spv)
    set(VULKAN_FRAGMENT_SPV ${VULKAN_SHADER_OUTPUT_DIR}/teaching.frag.spv)

    find_package(Vulkan REQUIRED)
    find_program(GLSLANG_VALIDATOR NAMES glslangValidator glslangValidator.exe REQUIRED)

    add_custom_command(
        OUTPUT ${VULKAN_VERTEX_SPV}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${VULKAN_SHADER_OUTPUT_DIR}
        COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.0 -o ${VULKAN_VERTEX_SPV} ${VULKAN_VERTEX_SHADER}
        DEPENDS ${VULKAN_VERTEX_SHADER}
        VERBATIM
    )

    add_custom_command(
        OUTPUT ${VULKAN_FRAGMENT_SPV}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${VULKAN_SHADER_OUTPUT_DIR}
        COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.0 -o ${VULKAN_FRAGMENT_SPV} ${VULKAN_FRAGMENT_SHADER}
        DEPENDS ${VULKAN_FRAGMENT_SHADER}
        VERBATIM
    )

    add_custom_target(vulkan_shaders
        DEPENDS
            ${VULKAN_VERTEX_SPV}
            ${VULKAN_FRAGMENT_SPV}
    )

    add_executable(${APP_NAME} WIN32
        ${DIRECTX_SOURCES}
        ${DIRECTX_HEADERS}
    )

    add_dependencies(${APP_NAME} vulkan_shaders)

    source_group(TREE ${SRC_DIR} FILES
        ${DIRECTX_SOURCES}
        ${DIRECTX_HEADERS}
    )

    target_include_directories(${APP_NAME} PRIVATE
        ${DIRECTX_SRC_DIR}
        ${OPENGL_SRC_DIR}
        ${VULKAN_SRC_DIR}
        ${SHARED_SRC_DIR}
    )

    target_link_libraries(${APP_NAME} PRIVATE
        d3d11
        d3d12
        dxgi
        d3dcompiler
        user32
        gdi32
        opengl32
        Vulkan::Vulkan
    )

    add_custom_command(TARGET ${APP_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${APP_NAME}>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VULKAN_VERTEX_SPV} "$<TARGET_FILE_DIR:${APP_NAME}>/shaders/teaching.vert.spv"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VULKAN_FRAGMENT_SPV} "$<TARGET_FILE_DIR:${APP_NAME}>/shaders/teaching.frag.spv"
    )
else()
    add_executable(${APP_NAME}
        ${OPENGL_SRC_DIR}/OpenGLCrossPlatformMain.cpp
    )

    find_package(OpenGL REQUIRED)
    find_package(glfw3 REQUIRED)

    target_include_directories(${APP_NAME} PRIVATE
        ${OPENGL_SRC_DIR}
    )

    target_link_libraries(${APP_NAME} PRIVATE
        OpenGL::GL
        glfw
    )
endif()
