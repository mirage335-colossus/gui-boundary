# Usage: cmake -P tools/verify-rev.cmake
get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
include("${root}/cmake/VerifyRevSources.cmake")
gui_verify_rev_sources("${root}")
message(STATUS "Preserved Rev sources, resources and dependency archives match their SHA-256 manifests.")
