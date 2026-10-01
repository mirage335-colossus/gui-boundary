# Build only the two portable libraries Rev needs, from preserved local archives.
# The platform SDK supplies OpenGL and (on Linux) X11. There is no network fallback.
function(gui_rev_extract archive digest directory)
    if(NOT EXISTS "${archive}")
        message(FATAL_ERROR "Missing preserved Rev dependency: ${archive}. Restore it from this example's source package; builds do not download dependencies.")
    endif()
    file(SHA256 "${archive}" actual)
    if(NOT actual STREQUAL digest)
        message(FATAL_ERROR "Checksum mismatch for preserved Rev dependency: ${archive}")
    endif()
    set(stamp "${directory}/.archive-sha256")
    if(EXISTS "${stamp}")
        file(READ "${stamp}" extracted)
        if(extracted STREQUAL digest)
            return()
        endif()
    endif()
    file(REMOVE_RECURSE "${directory}")
    file(MAKE_DIRECTORY "${directory}")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${directory}")
    file(WRITE "${stamp}" "${digest}")
endfunction()

function(gui_rev_dependencies)
    set(root "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rev-deps")
    set(staged "${CMAKE_CURRENT_BINARY_DIR}/rev-dependencies")
    gui_rev_extract("${root}/glew-2.3.1.tgz"
        b64790f94b926acd7e8f84c5d6000a86cb43967bd1e688b03089079799c9e889
        "${staged}/glew-source")
    gui_rev_extract("${root}/freetype-2.14.3.tar.xz"
        36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f
        "${staged}/freetype-source")

    set(glew_source "${staged}/glew-source/glew-2.3.1")
    add_library(gui_rev_glew STATIC "${glew_source}/src/glew.c")
    target_include_directories(gui_rev_glew SYSTEM PUBLIC "${glew_source}/include")
    target_compile_definitions(gui_rev_glew PUBLIC GLEW_STATIC GLEW_NO_GLU)
    target_link_libraries(gui_rev_glew PUBLIC OpenGL::GL)
    if(UNIX)
        target_link_libraries(gui_rev_glew PUBLIC X11::X11)
    endif()

    # The embedded TrueType font needs none of FreeType's optional compression,
    # image or shaping libraries. Disable discovery so the dependency closure is fixed.
    set(BUILD_SHARED_LIBS OFF)
    set(SKIP_INSTALL_ALL ON)
    foreach(dependency ZLIB BZIP2 PNG HARFBUZZ BROTLI)
        set(FT_DISABLE_${dependency} ON)
        set(FT_REQUIRE_${dependency} OFF)
    endforeach()
    set(FT_DYNAMIC_HARFBUZZ OFF)
    add_subdirectory("${staged}/freetype-source/freetype-2.14.3"
        "${staged}/freetype-build" EXCLUDE_FROM_ALL)
    set(gui_rev_glew gui_rev_glew PARENT_SCOPE)
    set(gui_rev_freetype freetype PARENT_SCOPE)
endfunction()
