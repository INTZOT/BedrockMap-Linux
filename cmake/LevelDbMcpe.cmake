# Locates or builds leveldb-mcpe, the LevelDB fork with the zlib/libdeflate
# compression Minecraft Bedrock worlds use.
#
# bedrock-level ships prebuilt archives only for Windows, Android and wasm, so on
# Linux the dependency is built from source as a static library and shared by the
# application and the data library.
#
# Resolution order:
#   1. -DLEVELDB_MCPE_ROOT=<dir>            (source checkout, used as a subproject)
#   2. <project>/third/leveldb-mcpe         (pre-fetched checkout)
#   3. FetchContent from the pinned upstream commit, patched for a static build
#
# The module defines, in the caller scope:
#   LEVELDB_MCPE_INCLUDE_DIR  headers matching the built library (must be used
#                             instead of bedrock-level vendored copies so the
#                             ABI cannot drift)
#   leveldb_mcpe              the library target to link against
include_guard(GLOBAL)

# Imported targets are directory scoped, so the caller (the root CMakeLists and
# the bedrock-level target) needs ZLIB::ZLIB in its own scope as well.
find_package(ZLIB REQUIRED)

# bedrock-level's vendored leveldb headers expose a libdeflate-backed codec
# (ZlibCompressorBase::useLibdeflate); the public leveldb-mcpe has no such
# support, so it is added by leveldb-mcpe-libdeflate.patch.
find_path(LIBDEFLATE_INCLUDE_DIR NAMES libdeflate.h)
find_library(LIBDEFLATE_LIBRARY NAMES deflate)
if(NOT LIBDEFLATE_INCLUDE_DIR OR NOT LIBDEFLATE_LIBRARY)
    message(FATAL_ERROR
        "libdeflate was not found, but bedrock-level requires the libdeflate-backed "
        "leveldb compressor on Linux. Install it (libdeflate on Arch, libdeflate-dev "
        "on Debian/Ubuntu) or point LIBDEFLATE_INCLUDE_DIR/LIBDEFLATE_LIBRARY at it.")
endif()
message(STATUS "libdeflate: ${LIBDEFLATE_LIBRARY}")

set(LEVELDB_MCPE_PINNED_TAG "4846fc72c7eda860b1bcf6efc58920a9273da928" CACHE STRING
    "leveldb-mcpe commit used when the dependency has to be downloaded")
set(LEVELDB_MCPE_ROOT "${PROJECT_SOURCE_DIR}/third/leveldb-mcpe" CACHE PATH
    "Existing leveldb-mcpe source checkout to build (empty to download)")

set(_leveldb_static_patch "${CMAKE_CURRENT_LIST_DIR}/patches/leveldb-mcpe-linux.patch")
set(_leveldb_libdeflate_patch "${CMAKE_CURRENT_LIST_DIR}/patches/leveldb-mcpe-libdeflate.patch")

if(LEVELDB_MCPE_ROOT AND NOT EXISTS "${LEVELDB_MCPE_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "LEVELDB_MCPE_ROOT=${LEVELDB_MCPE_ROOT} does not look like a leveldb-mcpe checkout")
endif()

if(LEVELDB_MCPE_ROOT)
    # The checkout is a local dependency cache, so the patches are applied in
    # place once and detected by their marker on later configure runs.
    find_program(BEDROCKMAP_PATCH_EXECUTABLE NAMES patch)
    if(NOT BEDROCKMAP_PATCH_EXECUTABLE)
        message(FATAL_ERROR "The 'patch' utility is required to prepare ${LEVELDB_MCPE_ROOT}.")
    endif()

    file(READ "${LEVELDB_MCPE_ROOT}/CMakeLists.txt" _leveldb_cmake)
    if(NOT _leveldb_cmake MATCHES "add_library\\(leveldb_mcpe STATIC")
        message(STATUS "leveldb-mcpe: applying the Linux static-build patch")
        execute_process(
            COMMAND "${BEDROCKMAP_PATCH_EXECUTABLE}" -p1 -d "${LEVELDB_MCPE_ROOT}" -i "${_leveldb_static_patch}"
            RESULT_VARIABLE _patch_result OUTPUT_VARIABLE _patch_out ERROR_VARIABLE _patch_err)
        if(NOT _patch_result EQUAL 0)
            message(FATAL_ERROR "Failed to apply ${_leveldb_static_patch}:\n${_patch_out}\n${_patch_err}")
        endif()
    endif()

    file(READ "${LEVELDB_MCPE_ROOT}/include/leveldb/zlib_compressor.h" _leveldb_zlib_header)
    if(NOT _leveldb_zlib_header MATCHES "useLibdeflate")
        message(STATUS "leveldb-mcpe: applying the libdeflate codec patch")
        execute_process(
            COMMAND "${BEDROCKMAP_PATCH_EXECUTABLE}" -p1 -d "${LEVELDB_MCPE_ROOT}" -i "${_leveldb_libdeflate_patch}"
            RESULT_VARIABLE _patch_result OUTPUT_VARIABLE _patch_out ERROR_VARIABLE _patch_err)
        if(NOT _patch_result EQUAL 0)
            message(FATAL_ERROR "Failed to apply ${_leveldb_libdeflate_patch}:\n${_patch_out}\n${_patch_err}")
        endif()
    endif()

    message(STATUS "leveldb-mcpe: building from ${LEVELDB_MCPE_ROOT}")
    add_subdirectory("${LEVELDB_MCPE_ROOT}" "${CMAKE_BINARY_DIR}/leveldb-mcpe" EXCLUDE_FROM_ALL)
    set(LEVELDB_MCPE_INCLUDE_DIR "${LEVELDB_MCPE_ROOT}/include")
else()
    include(FetchContent)
    find_package(Git REQUIRED)
    message(STATUS "leveldb-mcpe: downloading pinned upstream commit ${LEVELDB_MCPE_PINNED_TAG}")
    FetchContent_Declare(leveldb_mcpe
        GIT_REPOSITORY https://github.com/Amulet-Team/leveldb-mcpe.git
        GIT_TAG ${LEVELDB_MCPE_PINNED_TAG}
        GIT_SHALLOW FALSE
        PATCH_COMMAND "${CMAKE_COMMAND}" -E env
                      "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${_leveldb_static_patch}"
                      COMMAND "${CMAKE_COMMAND}" -E env
                      "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${_leveldb_libdeflate_patch}"
    )
    FetchContent_MakeAvailable(leveldb_mcpe)
    set(LEVELDB_MCPE_INCLUDE_DIR "${leveldb_mcpe_SOURCE_DIR}/include")
endif()

if(NOT TARGET leveldb_mcpe)
    message(FATAL_ERROR "leveldb-mcpe target was not created")
endif()

# The library is plain C++ but inherits this project's CMake AUTOMOC/AUTOUIC/
# AUTORCC settings, which would emit a "no valid Qt version" author warning.
set_target_properties(leveldb_mcpe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# Upstream only defines the DLLX export macro on its own target, but every public
# header uses it (class DLLX Slice, ...). Consumers must see the same definition,
# so export an empty one for them (UNIX builds a static library).
target_compile_definitions(leveldb_mcpe PUBLIC DLLX=)

# Route ZlibCompressorBase through libdeflate when the compiler asks for it.
target_compile_definitions(leveldb_mcpe PRIVATE LEVELDB_HAVE_LIBDEFLATE=1)
target_include_directories(leveldb_mcpe PRIVATE "${LIBDEFLATE_INCLUDE_DIR}")
target_link_libraries(leveldb_mcpe PRIVATE "${LIBDEFLATE_LIBRARY}")

set(LEVELDB_MCPE_INCLUDE_DIR "${LEVELDB_MCPE_INCLUDE_DIR}" CACHE PATH "leveldb-mcpe headers")
