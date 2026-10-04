# Locates or builds leveldb-mcpe, the LevelDB fork with the zlib compression
# Minecraft Bedrock worlds use. bedrock-level ships only the Windows static
# libraries, so on Linux the dependency is built from source as a static library.
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

# Imported targets are directory scoped, so the caller (the root CMakeLists that
# links bedrock-level) needs ZLIB::ZLIB in its own scope as well.
find_package(ZLIB REQUIRED)

set(LEVELDB_MCPE_PINNED_TAG "4846fc72c7eda860b1bcf6efc58920a9273da928" CACHE STRING
    "leveldb-mcpe commit used when the dependency has to be downloaded")
set(LEVELDB_MCPE_ROOT "${PROJECT_SOURCE_DIR}/third/leveldb-mcpe" CACHE PATH
    "Existing leveldb-mcpe source checkout to build (empty to download)")

if(LEVELDB_MCPE_ROOT AND NOT EXISTS "${LEVELDB_MCPE_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "LEVELDB_MCPE_ROOT=${LEVELDB_MCPE_ROOT} does not look like a leveldb-mcpe checkout")
endif()

if(LEVELDB_MCPE_ROOT)
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
        PATCH_COMMAND ${GIT_EXECUTABLE} apply --whitespace=nowarn
                      "${CMAKE_CURRENT_LIST_DIR}/patches/leveldb-mcpe-linux.patch"
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
if(NOT WIN32)
    target_compile_definitions(leveldb_mcpe PUBLIC DLLX=)
endif()
set(LEVELDB_MCPE_INCLUDE_DIR "${LEVELDB_MCPE_INCLUDE_DIR}" CACHE PATH "leveldb-mcpe headers")
