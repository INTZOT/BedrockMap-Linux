cmake_minimum_required(VERSION 3.16)

set(CMAKE_PREFIX_PATH $ENV{QT_ROOT})
message(STATUS "QT_ROOT environment variable: $ENV{QT_ROOT}")
message(STATUS "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}")
if(POLICY CMP0074)
    cmake_policy(SET CMP0074 NEW)
endif()

project(BedrockMap VERSION 0.1 LANGUAGES CXX)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -finput-charset=utf-8 -fexec-charset=utf-8")
endif()


# bedrock-level is built from source as a subdirectory so app headers and the
# library can never drift apart. Its sample apps/unit tests are opt-in (OFF).
set(BEDROCK_LEVEL_ROOT "${PROJECT_SOURCE_DIR}/bedrock-level")
# The library's standalone scripts own its unit-test builds; keep the GUI build
# focused on the application and never pull GoogleTest into this build tree.
set(BEDROCK_LEVEL_BUILD_TESTS OFF CACHE BOOL "Disable bedrock-level tests in the GUI build" FORCE)

# bedrock-level only ships prebuilt Windows binaries of leveldb-mcpe (the LevelDB
# fork with zlib compression). On Linux the dependency is located or built from
# source first (see cmake/LevelDbMcpe.cmake) and handed to the library below.
if(UNIX)
    include(${PROJECT_SOURCE_DIR}/cmake/LevelDbMcpe.cmake)
endif()

add_subdirectory(${BEDROCK_LEVEL_ROOT} bedrock-level)

if(UNIX)
    # bedrock-level vendors LevelDB headers that do not exactly match the library
    # built above (in-class initialisers instead of an out-of-line Options ctor),
    # so put the pristine headers of the linked build in front of the vendored ones
    # and link the library itself.
    get_target_property(_bedrock_level_includes bedrock-level INCLUDE_DIRECTORIES)
    list(FIND _bedrock_level_includes "${BEDROCK_LEVEL_ROOT}/third" _vendored_leveldb_index)
    if(_vendored_leveldb_index GREATER -1)
        list(INSERT _bedrock_level_includes ${_vendored_leveldb_index} "${LEVELDB_MCPE_INCLUDE_DIR}")
    else()
        list(APPEND _bedrock_level_includes "${LEVELDB_MCPE_INCLUDE_DIR}")
    endif()
    set_target_properties(bedrock-level PROPERTIES INCLUDE_DIRECTORIES "${_bedrock_level_includes}")
    target_link_libraries(bedrock-level PRIVATE leveldb_mcpe ZLIB::ZLIB)
endif()

find_package(Qt6 REQUIRED COMPONENTS 
    Core 
    Widgets 
    Concurrent 
    OpenGL 
    OpenGLWidgets
    Svg
    Network
)

# lupdate/lrelease are optional: distributions frequently split the Linguist tools
# into a separate package (qt6-tools on Arch, qttools6-dev-tools on Debian). When
# they are missing, the translation catalogues are compiled with any lrelease that
# can be found (including the one bundled with PySide6) or skipped with a warning.
find_package(Qt6 QUIET COMPONENTS LinguistTools)

# QHexView hex widget library (vendored in third/). Disable its bundled example
# app (removed) and keep the find/replace dialogs which QHexView core needs.
set(QHEXVIEW_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(QHEXVIEW_ENABLE_DIALOGS ON CACHE BOOL "" FORCE)
set(QHEXVIEW_USE_QT5 OFF CACHE BOOL "" FORCE)
add_subdirectory(${PROJECT_SOURCE_DIR}/third/QHexView third/QHexView)

# The Windows resource script embeds the .ico into the executable; Linux uses the
# .desktop entry and the SVG icon installed further down instead.
set(APP_ICON_RESOURCE_WINDOWS "")
if(WIN32)
    set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/icon.rc")
endif()

file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
        src/*.ui
        src/*.cpp
        src/include/*.h
)

file(GLOB_RECURSE RESOURCE_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.png
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.jpg
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.svg
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/res/*.dat
)

# Generate the resource manifest in the build tree so configuring CMake never
# modifies a tracked source file.
set(QRC_FILE ${CMAKE_CURRENT_BINARY_DIR}/res.qrc)
file(WRITE ${QRC_FILE} "<RCC>\n    <qresource prefix=\"/\">\n")
foreach(FILE ${RESOURCE_FILES})
    file(RELATIVE_PATH REL_PATH ${CMAKE_CURRENT_SOURCE_DIR} ${FILE})
    string(REPLACE "\\" "/" REL_PATH ${REL_PATH})
    string(REPLACE "\\" "/" RESOURCE_PATH ${FILE})
    file(APPEND ${QRC_FILE} "        <file alias=\"${REL_PATH}\">${RESOURCE_PATH}</file>\n")
endforeach()
file(APPEND ${QRC_FILE} "    </qresource>\n</RCC>\n")

set(TS_FILES
    translations/zh_CN.ts
    translations/en.ts
)

add_executable(BedrockMap
    ${SRC_FILES}
    ${APP_ICON_RESOURCE_WINDOWS}
    ${QRC_FILE}
)

set(QM_FILES "")
if(TARGET Qt6::lrelease)
    qt6_add_translations(BedrockMap
        TS_FILES ${TS_FILES}
        QM_FILES_OUTPUT_VARIABLE QM_FILES
    )
else()
    find_program(BEDROCKMAP_LRELEASE_EXECUTABLE NAMES lrelease lrelease6 lrelease-qt6 pyside6-lrelease)
    if(BEDROCKMAP_LRELEASE_EXECUTABLE)
        message(STATUS "Qt LinguistTools not found; compiling translations with ${BEDROCKMAP_LRELEASE_EXECUTABLE}")
        foreach(TS_FILE ${TS_FILES})
            get_filename_component(TS_LANG ${TS_FILE} NAME_WE)
            set(TS_QM "${CMAKE_CURRENT_BINARY_DIR}/${TS_LANG}.qm")
            add_custom_command(
                OUTPUT ${TS_QM}
                COMMAND ${BEDROCKMAP_LRELEASE_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/${TS_FILE}" -qm "${TS_QM}"
                DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${TS_FILE}"
                COMMENT "Compiling translation ${TS_LANG}.qm"
                VERBATIM
            )
            list(APPEND QM_FILES ${TS_QM})
        endforeach()
        add_custom_target(BedrockMapTranslations ALL DEPENDS ${QM_FILES})
    else()
        message(WARNING
            "Qt LinguistTools, lrelease and pyside6-lrelease were all not found: "
            "the application will run with the untranslated (English) interface. "
            "Install qt6-tools (Arch) / qttools6-dev-tools (Debian) to enable translations.")
    endif()
endif()

target_include_directories(BedrockMap PRIVATE
    ${PROJECT_SOURCE_DIR}/src/include/core
    ${PROJECT_SOURCE_DIR}/src/include/ui
    ${PROJECT_SOURCE_DIR}/src/include
    ${PROJECT_SOURCE_DIR}/third
    # Same reason as for bedrock-level above: the headers must match the linked
    # leveldb-mcpe build (Windows keeps using the vendored Windows copies).
    $<$<BOOL:${LEVELDB_MCPE_INCLUDE_DIR}>:${LEVELDB_MCPE_INCLUDE_DIR}>
)

target_link_libraries(BedrockMap PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Svg
    Qt6::Network
    QHexView
    bedrock-level
)

if(UNIX)
    # The application includes LevelDB headers itself (leveltabwidget.h) and must
    # compile against the same build bedrock-level links; this also propagates the
    # DLLX export macro the upstream headers require.
    target_link_libraries(BedrockMap PRIVATE leveldb_mcpe)

    # The vendored Windows headers add DB::LastSequence(); upstream leveldb-mcpe
    # does not have it. Detect it so the debug dialog can fall back gracefully.
    file(READ "${LEVELDB_MCPE_INCLUDE_DIR}/leveldb/db.h" BEDROCKMAP_LEVELDB_DB_HEADER)
    if(BEDROCKMAP_LEVELDB_DB_HEADER MATCHES "LastSequence")
        target_compile_definitions(BedrockMap PRIVATE BEDROCKMAP_LEVELDB_HAS_LAST_SEQUENCE=1)
    endif()
else()
    # The vendored Windows headers always provide it.
    target_compile_definitions(BedrockMap PRIVATE BEDROCKMAP_LEVELDB_HAS_LAST_SEQUENCE=1)
endif()

target_precompile_headers(BedrockMap PRIVATE
    src/include/pch.h
)

target_compile_definitions(BedrockMap PRIVATE NOMINMAX)

# -rdynamic exports the executable's own symbols so the Linux crash handler can
# resolve function names out of the backtrace without a debugger attached, and the
# line tables kept by -g1 let addr2line turn the logged addresses into file:line
# (same trade-off as the MinGW branch below: no full -g bloat).
if(UNIX AND NOT APPLE)
    target_link_options(BedrockMap PRIVATE -rdynamic)
    if(BEDROCKMAP_FULL_DEBUG)
        target_compile_options(BedrockMap PRIVATE -g)
    else()
        # Debug keeps the standard full -g; the other configurations get line
        # tables only.
        target_compile_options(BedrockMap PRIVATE $<$<NOT:$<CONFIG:Debug>>:-g1>)
    endif()
endif()

execute_process(
    COMMAND git rev-parse --short=7 HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_COMMIT_HASH)
    set(GIT_COMMIT_HASH "unknown")
endif()
message(STATUS "Git commit hash: ${GIT_COMMIT_HASH}")
target_compile_definitions(BedrockMap PRIVATE GIT_COMMIT_HASH="${GIT_COMMIT_HASH}")

# OFF by default: -g1 keeps crash-handler backtraces symbolized without the
# ~90MB of full debug info. ON gives full -g for gdb local-variable debugging.
option(BEDROCKMAP_FULL_DEBUG "Build with full -g debug info (for gdb)" OFF)

if(WIN32)
    if(MINGW)
        # crashhandler.cpp unwinds via the RBP frame chain and symbolizes with
        # libbacktrace (reads the DWARF from the exe). -g1 is enough for that:
        # it keeps function names + file/line but drops local vars & full type
        # info, which slashes the ~90MB of debug bloat. Turn BEDROCKMAP_FULL_DEBUG
        # on (cmake -DBEDROCKMAP_FULL_DEBUG=ON) if you need full -g for gdb.
        if(BEDROCKMAP_FULL_DEBUG)
            set(_dbg_flag -g)
        else()
            set(_dbg_flag -g1)
        endif()
        target_compile_options(BedrockMap PRIVATE ${_dbg_flag} -fno-omit-frame-pointer)
        # ntdll exports RtlLookupFunctionEntry/RtlVirtualUnwind, which the crash
        # handler uses to unwind via the per-function .pdata/.xdata metadata.
        target_link_libraries(BedrockMap PRIVATE backtrace ntdll)
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(BedrockMap PRIVATE
            -static-libgcc
            -static-libstdc++
            -mconsole
        )
    else()
        target_link_options(BedrockMap PRIVATE
            -static-libgcc
            -static-libstdc++
            -mwindows
        )
    endif()
endif()


if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    target_compile_definitions(BedrockMap PRIVATE QT_NO_DEBUG_OUTPUT)
endif()

# ---------------------------------------------------------------------------
# Linux/POSIX install layout (cmake --install). Windows keeps using deploy.ps1.
#   <prefix>/bin/BedrockMap
#   <prefix>/share/BedrockMap/{block_color.json,biome_color.json,translations/}
#   <prefix>/share/applications/BedrockMap.desktop
#   <prefix>/share/icons/hicolor/scalable/apps/BedrockMap.svg
# ---------------------------------------------------------------------------
if(UNIX)
    include(GNUInstallDirs)

    install(TARGETS BedrockMap RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    # Colour tables are runtime data, not compiled resources: they are copied next
    # to the executable so the packaging stays inspectable and replaceable.
    install(FILES
        "${BEDROCK_LEVEL_ROOT}/data/colors/block_color.json"
        "${BEDROCK_LEVEL_ROOT}/data/colors/biome_color.json"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/BedrockMap
    )

    if(QM_FILES)
        install(FILES ${QM_FILES} DESTINATION ${CMAKE_INSTALL_DATADIR}/BedrockMap/translations)
    endif()

    install(FILES "${PROJECT_SOURCE_DIR}/packaging/BedrockMap.desktop"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES "${PROJECT_SOURCE_DIR}/BedrockMap.svg"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
            RENAME BedrockMap.svg)
endif()

