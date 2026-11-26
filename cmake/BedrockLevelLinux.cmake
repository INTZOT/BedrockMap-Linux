# Builds the bedrock-level submodule on platforms its own CMakeLists rejects.
#
# The submodule's build only supports the platforms it ships prebuilt
# leveldb-mcpe archives for (MinGW, Android arm64-v8a, wasm32) and stops with a
# fatal error everywhere else. On Linux those archives do not exist, so the
# dependency is built from source (see LevelDbMcpe.cmake) and the library target
# is created here with the same sources, standard and include layout.
include_guard(GLOBAL)

if(TARGET bedrock-level)
    return()
endif()

set(_bedrock_level_root "${PROJECT_SOURCE_DIR}/bedrock-level")

file(GLOB BEDROCK_LEVEL_SOURCES CONFIGURE_DEPENDS
    "${_bedrock_level_root}/src/*.cpp"
)
list(APPEND BEDROCK_LEVEL_SOURCES
    "${_bedrock_level_root}/third/loguru/loguru.cpp"
    "${_bedrock_level_root}/third/stb/stb_image_write.cpp"
    # NetEase ("mcne") XOR codec: only declared in the submodule's vendored
    # leveldb/mcne.h, implemented here because the public leveldb-mcpe lacks it.
    "${PROJECT_SOURCE_DIR}/cmake/leveldb-mcne/mcne.cc"
)

add_library(bedrock-level STATIC ${BEDROCK_LEVEL_SOURCES})

# Standalone data library: never inherit this project's Qt AUTOGEN settings.
set_target_properties(bedrock-level PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

# The pristine headers of the library that is actually linked must win over the
# submodule's vendored copies so the ABI cannot drift; the vendored tree still
# provides loguru, stb and the remaining headers.
target_include_directories(bedrock-level PUBLIC
    "${_bedrock_level_root}/src/include"
    "${LEVELDB_MCPE_INCLUDE_DIR}"
    "${_bedrock_level_root}/third"
)

# PUBLIC, not PRIVATE: consumers of this static library include leveldb's
# public headers and need the DLLX export macro that leveldb_mcpe exports as a
# usage requirement. PRIVATE would only put the library on the link line and
# drop those compile definitions, leaving the headers unparsable.
target_link_libraries(bedrock-level PUBLIC leveldb_mcpe ZLIB::ZLIB "${LIBDEFLATE_LIBRARY}")
