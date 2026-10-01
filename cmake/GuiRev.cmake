# The optional toolkit uses modules; the public boundary and application remain C++20.
if(CMAKE_VERSION VERSION_LESS 3.28)
    message(FATAL_ERROR "GUI_BUILD_REV requires CMake 3.28 or newer for C++ modules.")
endif()
if(NOT CMAKE_GENERATOR MATCHES "^(Ninja|Ninja Multi-Config|Visual Studio 17 2022|Visual Studio 18 2026)$")
    message(FATAL_ERROR "GUI_BUILD_REV requires Ninja, Ninja Multi-Config or Visual Studio 2022+.")
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15)
        message(FATAL_ERROR "Rev needs GCC 15+ or Clang 19+. GCC 14 has a module compiler defect for this snapshot. Configure a fresh build with clang++-19 and Ninja.")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19)
        message(FATAL_ERROR "Rev requires Clang 19 or newer, including its matching clang-scan-deps tool.")
    endif()
elseif(MSVC)
    if(MSVC_VERSION LESS 1936)
        message(FATAL_ERROR "Rev requires Visual Studio 2022 17.6 or newer for C++ modules.")
    endif()
else()
    message(FATAL_ERROR "Rev requires Clang 19+, GCC 15+ or Visual Studio 2022 17.6+.")
endif()
if(WIN32)
    set(gui_rev_platform win)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(gui_rev_platform lnx)
else()
    message(FATAL_ERROR "This Rev integration supports Linux/X11 and Windows/OpenGL only.")
endif()

option(GUI_REV_BUNDLED_DEPS "Build preserved local GLEW/FreeType sources instead of using installed libraries" ${WIN32})
include("${CMAKE_CURRENT_LIST_DIR}/VerifyRevSources.cmake")
gui_verify_rev_sources("${CMAKE_CURRENT_SOURCE_DIR}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rev/SHA256SUMS"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rev-deps/SHA256SUMS")
find_package(OpenGL REQUIRED)
if(UNIX)
    find_package(X11 REQUIRED COMPONENTS Xrandr Xext)
endif()
if(GUI_REV_BUNDLED_DEPS)
    enable_language(C)
    include("${CMAKE_CURRENT_LIST_DIR}/RevDependencies.cmake")
    gui_rev_dependencies()
else()
    find_package(GLEW REQUIRED)
    find_package(Freetype REQUIRED)
    set(gui_rev_glew GLEW::GLEW)
    set(gui_rev_freetype Freetype::Freetype)
endif()

set(gui_rev_root "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rev")
set(gui_rev_generated "${CMAKE_CURRENT_BINARY_DIR}/rev-generated")
file(GLOB_RECURSE gui_rev_inputs CONFIGURE_DEPENDS
    "${gui_rev_root}/source/*" "${gui_rev_root}/resources/*" "${gui_rev_root}/external/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${gui_rev_inputs})
include("${CMAKE_CURRENT_LIST_DIR}/PrepareRev.cmake")
gui_prepare_rev("${gui_rev_root}" "${gui_rev_generated}" "${gui_rev_platform}")
add_library(gui_rev_toolkit STATIC "${gui_rev_generated}/rev_embedded.cpp")
set_target_properties(gui_rev_toolkit PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF CXX_SCAN_FOR_MODULES ON)
target_sources(gui_rev_toolkit PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES
    BASE_DIRS "${gui_rev_generated}" FILES ${GUI_REV_MODULES})
target_include_directories(gui_rev_toolkit SYSTEM PUBLIC
    "${gui_rev_root}/external" "${gui_rev_root}/source/OS/Window" "${gui_rev_generated}")
target_compile_definitions(gui_rev_toolkit PRIVATE PROJECT_ROOT="${gui_rev_root}")
target_compile_definitions(gui_rev_toolkit PUBLIC PLATFORM_OPENGL)
if(MSVC)
    target_compile_options(gui_rev_toolkit PRIVATE /utf-8)
endif()
target_link_libraries(gui_rev_toolkit PUBLIC OpenGL::GL ${gui_rev_glew} ${gui_rev_freetype})
if(WIN32)
    target_link_libraries(gui_rev_toolkit PUBLIC user32 gdi32 ole32 shell32 uuid dwmapi shcore setupapi)
else()
    target_link_libraries(gui_rev_toolkit PUBLIC X11::X11 X11::Xrandr X11::Xext)
endif()

add_library(gui_rev_adapter STATIC backends/rev/adapter.cpp backends/rev/clipboard.cpp)
target_include_directories(gui_rev_adapter PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/backends/rev")
target_link_libraries(gui_rev_adapter PUBLIC gui_boundary PRIVATE gui_rev_toolkit)
set_target_properties(gui_rev_adapter PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF CXX_SCAN_FOR_MODULES ON)
gui_warnings(gui_rev_adapter)
gui_executable(gui_rev_demo backends/rev/main.cpp)
target_link_libraries(gui_rev_demo PRIVATE gui_rev_adapter)

# Keep all dependency notices beside any installed executable. No build step fetches sources.
install(TARGETS gui_rev_demo RUNTIME DESTINATION bin)
install(FILES "${gui_rev_root}/README.md" "${gui_rev_root}/UPSTREAM.json"
    "${gui_rev_root}/PATCHES.md" "${gui_rev_root}/SHA256SUMS"
    DESTINATION share/doc/gui-boundary/rev)
install(FILES "${gui_rev_root}/resources/DejaVu-LICENSE"
    "${gui_rev_root}/resources/DejaVu-AUTHORS" "${gui_rev_root}/resources/DejaVu-SOURCES.json"
    DESTINATION share/doc/gui-boundary/rev)
install(FILES "${gui_rev_root}/external/glm/LICENSE"
    DESTINATION share/doc/gui-boundary/rev RENAME GLM-LICENSE)
install(FILES "${gui_rev_root}/external/nanosvg/LICENSE"
    DESTINATION share/doc/gui-boundary/rev RENAME NANOSVG-LICENSE)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rev-deps/glew-LICENSE.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rev-deps/freetype-LICENSE.TXT"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rev-deps/freetype-FTL.TXT"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rev-deps/freetype-GPLv2.TXT"
    DESTINATION share/doc/gui-boundary/rev)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rev-deps/README.md"
    DESTINATION share/doc/gui-boundary/rev RENAME DEPENDENCIES.md)
