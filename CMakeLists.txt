cmake_minimum_required(VERSION 3.16)
project(echelon C)

# --- Pull in the Arcadia SDK -------------------------------------------------
# Fetched as a pinned HTTPS tarball of a specific commit — no Git required
# (CMake downloads it with its built-in libcurl). To update, change the commit
# in the URL and recompute the hash:
#   curl -sL <url> | sha256sum
# For local development, point at a sibling checkout to skip the download:
#   -DFETCHCONTENT_SOURCE_DIR_ARCADIA_SDK=../arcadia-sdk
include(FetchContent)
FetchContent_Declare(arcadia_sdk
    URL      https://github.com/skeeto/arcadia-sdk/archive/db899d072f58496c0b1d23e2dcd28e6e11e46144.tar.gz
    URL_HASH SHA256=b64b82526966128280050e1b5afd228fdef0f2e13edfe55a99e568fba24a5404
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(arcadia_sdk)   # SDK samples aren't built as a subproject.

# add_arcadia_toy() and its ARCADIA_SDK_* path vars are set in the SDK's own
# subdirectory scope, which FetchContent does not propagate up to us. Re-include
# the helper here so the vars resolve in this scope (the arcadia_toy_runtime
# target is global and guarded, so nothing is duplicated).
list(APPEND CMAKE_MODULE_PATH "${arcadia_sdk_SOURCE_DIR}/cmake")
include(ArcadiaToy)

# --- The toy -----------------------------------------------------------------
add_arcadia_toy(echelon
    NAME    "Echelon"
    NUMBER  13                 # folder becomes toy13; registry slot HKCU\...\TOYS\TOY13
    VERSION "0.0001"
    DESC    "Co-op vertical shooter"
    SOURCES src/echelon.c
            src/world.c
            src/sim.c
            src/roles.c
            src/clock.c
            src/net.c
            src/snapshot.c
            src/glfont.c
            src/render.c
    ASSETS  "${CMAKE_CURRENT_SOURCE_DIR}/sfx")

target_link_libraries(echelon PRIVATE opengl32 winmm)
target_compile_options(echelon PRIVATE -Wall -Wextra)

# --- Deploy to the two local test installs -----------------------------------
# install_arcadia_toy_to() names its target <target>_deploy and can only be
# called once, so we roll our own copy targets for arcadia/ and arcadia2/.
get_target_property(_stage  echelon ARCADIA_STAGE_DIR)
get_target_property(_folder echelon ARCADIA_FOLDER)

add_custom_target(deploy
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${_stage}" "${CMAKE_SOURCE_DIR}/arcadia/Toys/${_folder}"
    DEPENDS echelon
    COMMENT "Deploying Echelon -> arcadia/Toys/${_folder}")

add_custom_target(deploy2
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${_stage}" "${CMAKE_SOURCE_DIR}/arcadia2/Toys/${_folder}"
    DEPENDS echelon
    COMMENT "Deploying Echelon -> arcadia2/Toys/${_folder}")

# Convenience: build + deploy to both installs.
add_custom_target(deploy_all DEPENDS deploy deploy2)
