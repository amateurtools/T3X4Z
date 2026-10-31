cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------------
# Settings that must exist BEFORE project() so they reach every target (JUCE's too)
# ---------------------------------------------------------------------------
if(APPLE)
    # Universal binary. CACHE without FORCE so `-DCMAKE_OSX_ARCHITECTURES=arm64` still overrides.
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0"     CACHE STRING "Minimum macOS version")
endif()
if(WIN32)
    # Static MSVC runtime: users don't need the VC++ redistributable installed.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "MSVC runtime")
endif()

project(T3X4Z VERSION 0.1.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(T3X4Z_BUILD_PLUGIN  "Build the JUCE plugin (needs JUCE)"                               ON)
option(T3X4Z_BUILD_CLAP    "Also build a CLAP plugin (needs clap-juce-extensions)"           ON)
option(T3X4Z_BUILD_TOOLS   "Build the headless boot_test tool"                               ON)
option(T3X4Z_BUILD_TESTS   "Build the ProcessorTest host harness"                           ON)
option(T3X4Z_FETCH_DEPS    "Download JUCE / clap-juce-extensions if not found locally"       OFF)
option(T3X4Z_NATIVE_ARCH   "Tune for THIS cpu (-march=native). Local use only, never ship"   OFF)
option(T3X4Z_LTO           "Link-time optimisation (slower builds; no measurable gain here)" OFF)
option(T3X4Z_INSTALL_AFTER_BUILD "Copy plugins into the user plugin folders after building" OFF)

set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE" CACHE PATH "Path to a JUCE 8 checkout")
set(CLAP_JUCE_EXTENSIONS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/clap-juce-extensions" CACHE PATH
    "Path to a clap-juce-extensions checkout (with submodules)")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)   # the core is linked into shared plugin modules

# Single-config generators (Make/Ninja) default to Release; multi-config (VS/Xcode) pick at build time.
get_property(T3X4Z_MULTI GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT T3X4Z_MULTI AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Shared compile settings, expressed per-config with generator expressions so they
# also work with Visual Studio / Xcode (where CMAKE_BUILD_TYPE is empty).
# Deliberately NO -ffast-math: the core is integer code, measured gain is nil.
# ---------------------------------------------------------------------------
add_library(t3x4z_flags INTERFACE)
if(T3X4Z_NATIVE_ARCH)
    target_compile_options(t3x4z_flags INTERFACE
        $<IF:$<CXX_COMPILER_ID:MSVC>,/arch:AVX2,-march=native>)
endif()
if(T3X4Z_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT T3X4Z_IPO_OK OUTPUT T3X4Z_IPO_MSG)
    if(T3X4Z_IPO_OK)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    else()
        message(WARNING "LTO requested but not supported: ${T3X4Z_IPO_MSG}")
    endif()
endif()

# ---------------------------------------------------------------------------
# Core: HD6303X + TX81Z machine + ymfm OPZ (no JUCE, builds anywhere)
# ---------------------------------------------------------------------------
add_library(tx81z_core STATIC
    src/hd6303x.cpp
    src/tx81z.cpp
    third_party/ymfm/ymfm_opz.cpp)
target_include_directories(tx81z_core PUBLIC src third_party/ymfm)
target_link_libraries(tx81z_core PUBLIC t3x4z_flags)
# Hidden symbols: the plugin is loaded into a host next to other plugins that may embed their own ymfm.
set_target_properties(tx81z_core PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

if(T3X4Z_BUILD_TOOLS)
    add_executable(boot_test tools/boot_test.cpp)
    target_link_libraries(boot_test PRIVATE tx81z_core)
endif()

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
if(T3X4Z_BUILD_PLUGIN)
    include(FetchContent)
    if(EXISTS "${JUCE_DIR}/CMakeLists.txt")
        add_subdirectory("${JUCE_DIR}" juce EXCLUDE_FROM_ALL)
    elseif(T3X4Z_FETCH_DEPS)
        FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
                                  GIT_TAG 8.0.4 GIT_SHALLOW ON)
        FetchContent_MakeAvailable(juce)
    else()
        message(WARNING "JUCE not found at ${JUCE_DIR}: building the core only. "
                        "Pass -DJUCE_DIR=<path> or -DT3X4Z_FETCH_DEPS=ON to build the plugin.")
        set(T3X4Z_BUILD_PLUGIN OFF)
    endif()
endif()

if(T3X4Z_BUILD_PLUGIN)
    if(T3X4Z_BUILD_CLAP)
        if(EXISTS "${CLAP_JUCE_EXTENSIONS_DIR}/CMakeLists.txt")
            add_subdirectory("${CLAP_JUCE_EXTENSIONS_DIR}" clap-juce-extensions EXCLUDE_FROM_ALL)
        elseif(T3X4Z_FETCH_DEPS)
            FetchContent_Declare(clap_juce_extensions
                GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
                GIT_TAG main GIT_SHALLOW ON GIT_SUBMODULES_RECURSE ON)
            FetchContent_MakeAvailable(clap_juce_extensions)
        else()
            message(WARNING "clap-juce-extensions not found at ${CLAP_JUCE_EXTENSIONS_DIR}: no CLAP build. "
                            "Pass -DCLAP_JUCE_EXTENSIONS_DIR=<path> or -DT3X4Z_FETCH_DEPS=ON.")
            set(T3X4Z_BUILD_CLAP OFF)
        endif()
    endif()
    add_subdirectory(plugin)
endif()

# ---------------------------------------------------------------------------
message(STATUS "T3X4Z: build type '${CMAKE_BUILD_TYPE}' (multi-config=${T3X4Z_MULTI})  plugin=${T3X4Z_BUILD_PLUGIN}  clap=${T3X4Z_BUILD_CLAP}"
               "  native-arch=${T3X4Z_NATIVE_ARCH}  lto=${T3X4Z_LTO}")