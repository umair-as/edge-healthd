cmake_minimum_required(VERSION 3.20)

# Derive version from git tag (v0.5.1 → 0.5.1) with fallback to VERSION file.
# Sets GIT_DERIVED_VERSION and EDGE_HEALTHD_VERSION_DETAILED.
include(cmake/GetGitVersion.cmake)

project(edge-healthd
    VERSION ${GIT_DERIVED_VERSION}
    DESCRIPTION "Edge device health monitoring daemon"
    LANGUAGES CXX
)

configure_file(inc/version.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/inc/version.hpp @ONLY)

# -----------------------------------------------------------------------------
# C++ Standard and Compiler Requirements
# -----------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -----------------------------------------------------------------------------
# Build Options
# -----------------------------------------------------------------------------

option(EDGE_BUILD_TESTS "Build unit tests" OFF)
option(EDGE_ENABLE_SANITIZERS "Enable ASan + UBSan (Debug builds)" OFF)
option(EDGE_ENABLE_LTO "Enable Link-Time Optimization (Release builds)" ON)
option(EDGE_FETCH_SDBUSCPP "Fetch sdbus-c++ if not found" OFF)
option(EDGE_WEB_UI "Build the web UI server" OFF)

# -----------------------------------------------------------------------------
# Compiler Warnings (strict by default)
# -----------------------------------------------------------------------------

set(EDGE_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Werror=return-type
    -Werror=uninitialized
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Woverloaded-virtual
    -Wnull-dereference
    -Wformat=2
    -Wdouble-promotion
    -Wimplicit-fallthrough
)

# -----------------------------------------------------------------------------
# Sanitizers (Debug builds)
# -----------------------------------------------------------------------------

if(EDGE_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(SANITIZER_FLAGS "-fsanitize=address,undefined -fno-omit-frame-pointer")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${SANITIZER_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} ${SANITIZER_FLAGS}")
    message(STATUS "Sanitizers enabled: ASan + UBSan")
endif()

# -----------------------------------------------------------------------------
# LTO (Release builds)
# -----------------------------------------------------------------------------

if(EDGE_ENABLE_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT lto_supported)
    if(lto_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
        message(STATUS "LTO enabled")
    endif()
endif()

# -----------------------------------------------------------------------------
# Exploit-mitigation hardening
#
# A daemon that parses netlink, journald, D-Bus and /proc,/sys text should ship
# hardened regardless of the distro. A Yocto build injects most of these via its
# security flags; applying them here too makes native/upstream builds hardened
# and is idempotent (FORTIFY is undef'd first to avoid a redefinition warning).
# Disabled automatically under sanitizers, which are incompatible with FORTIFY /
# stack-protection instrumentation.
# -----------------------------------------------------------------------------

option(EDGE_ENABLE_HARDENING "Enable exploit-mitigation build flags" ON)
set(EDGE_HARDENING_COMPILE "")
set(EDGE_HARDENING_LINK "")

if(EDGE_ENABLE_HARDENING AND NOT EDGE_ENABLE_SANITIZERS)
    include(CheckCXXCompilerFlag)
    list(APPEND EDGE_HARDENING_COMPILE -fstack-protector-strong -D_GLIBCXX_ASSERTIONS -fPIE)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        # _FORTIFY_SOURCE needs an optimizing build (>= -O1).
        list(APPEND EDGE_HARDENING_COMPILE -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2)
    endif()
    check_cxx_compiler_flag("-fstack-clash-protection" EDGE_HAS_STACK_CLASH)
    if(EDGE_HAS_STACK_CLASH)
        list(APPEND EDGE_HARDENING_COMPILE -fstack-clash-protection)
    endif()
    # aarch64 pointer-authentication + BTI (skipped where unsupported, e.g. x86).
    check_cxx_compiler_flag("-mbranch-protection=standard" EDGE_HAS_BRANCH_PROT)
    if(EDGE_HAS_BRANCH_PROT)
        list(APPEND EDGE_HARDENING_COMPILE -mbranch-protection=standard)
    endif()
    list(APPEND EDGE_HARDENING_LINK -pie -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack)
    message(STATUS "Hardening flags enabled")
endif()

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------

# nlohmann/json (header-only)
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found, fetching...")
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        # Pinned to the commit SHA of v3.11.3 so a re-pointed tag can't change
        # the fetched source. GIT_SHALLOW is off because a shallow clone of an
        # arbitrary (non-tip) commit is not reliably supported.
        GIT_TAG 9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03 # v3.11.3
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# libsystemd (optional, for sd-bus, sd-journal)
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(SYSTEMD IMPORTED_TARGET libsystemd)
    pkg_check_modules(LIBMNL IMPORTED_TARGET libmnl)
    pkg_check_modules(SDBUSCPP IMPORTED_TARGET sdbus-c++>=2.0.0)
endif()

if(SYSTEMD_FOUND)
    message(STATUS "libsystemd found: ${SYSTEMD_VERSION}")
    set(EDGE_HAS_SYSTEMD TRUE)
else()
    message(STATUS "libsystemd not found, using fallback implementations")
    set(EDGE_HAS_SYSTEMD FALSE)
endif()

if(LIBMNL_FOUND)
    message(STATUS "libmnl found: ${LIBMNL_VERSION}")
    set(EDGE_HAS_LIBMNL TRUE)
else()
    message(STATUS "libmnl not found, sdbus-c++ may not build")
    set(EDGE_HAS_LIBMNL FALSE)
endif()

set(SDBUSCPP_TARGET "")
if(SDBUSCPP_FOUND)
    set(SDBUSCPP_TARGET PkgConfig::SDBUSCPP)
    message(STATUS "sdbus-c++ found: ${SDBUSCPP_VERSION}")
elseif(EDGE_FETCH_SDBUSCPP)
    message(STATUS "sdbus-c++ not found; fetching v2.0.0")
    include(FetchContent)
    FetchContent_Declare(
        sdbus_cpp
        GIT_REPOSITORY https://github.com/Kistler-Group/sdbus-cpp.git
        # Pinned to the commit SHA of v2.0.0 (supply-chain: mutable tags can move).
        GIT_TAG b7d85f936d622299ee57966c2b2aefde5e4f3684 # v2.0.0
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(sdbus_cpp)

    if(TARGET sdbus-c++::sdbus-c++)
        set(SDBUSCPP_TARGET sdbus-c++::sdbus-c++)
    elseif(TARGET sdbus-c++)
        set(SDBUSCPP_TARGET sdbus-c++)
    elseif(TARGET sdbus-c++-static)
        set(SDBUSCPP_TARGET sdbus-c++-static)
    endif()

    if(SDBUSCPP_TARGET STREQUAL "")
        message(FATAL_ERROR "sdbus-c++ fetched but no known target found")
    endif()
else()
    message(FATAL_ERROR "sdbus-c++ >= 2.0.0 not found (set EDGE_FETCH_SDBUSCPP=ON to fetch)")
endif()

# -----------------------------------------------------------------------------
# Library: edge-snapshot-core (shared between daemon and tests)
# -----------------------------------------------------------------------------

add_library(edge-snapshot-core STATIC
    src/atomic_file.cpp
    src/config.cpp
    src/probes.cpp
    src/probes_dbus.cpp
    src/journal.cpp
    src/netlink_monitor.cpp
    src/ethtool_link.cpp
    src/aggregator.cpp
    src/writer.cpp
    src/json.cpp
    src/dbus_manager.cpp
)

target_include_directories(edge-snapshot-core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/inc>
        $<INSTALL_INTERFACE:inc>
)

target_compile_options(edge-snapshot-core PRIVATE ${EDGE_WARNINGS} ${EDGE_HARDENING_COMPILE})

target_link_libraries(edge-snapshot-core
    PUBLIC
        nlohmann_json::nlohmann_json
        ${SDBUSCPP_TARGET}
)

get_target_property(SDBUSCPP_INCLUDE_DIRS ${SDBUSCPP_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
if(SDBUSCPP_INCLUDE_DIRS)
    target_include_directories(edge-snapshot-core SYSTEM PUBLIC ${SDBUSCPP_INCLUDE_DIRS})
endif()

if(EDGE_HAS_SYSTEMD)
    target_link_libraries(edge-snapshot-core PUBLIC PkgConfig::SYSTEMD)
    target_compile_definitions(edge-snapshot-core PUBLIC EDGE_HAS_SYSTEMD=1)
endif()

if(EDGE_HAS_LIBMNL)
    target_link_libraries(edge-snapshot-core PUBLIC PkgConfig::LIBMNL)
    target_compile_definitions(edge-snapshot-core PUBLIC EDGE_HAS_LIBMNL=1)
endif()
# -----------------------------------------------------------------------------
# Executable: edge-healthd
# -----------------------------------------------------------------------------

add_executable(edge-healthd
    src/main.cpp
    src/daemon.cpp
)

target_compile_options(edge-healthd PRIVATE ${EDGE_WARNINGS} ${EDGE_HARDENING_COMPILE})
target_link_options(edge-healthd PRIVATE ${EDGE_HARDENING_LINK})

target_link_libraries(edge-healthd PRIVATE edge-snapshot-core)

# Strip in release
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(TARGET edge-healthd POST_BUILD
        COMMAND ${CMAKE_STRIP} --strip-unneeded $<TARGET_FILE:edge-healthd>
        COMMENT "Stripping edge-healthd"
    )
endif()

# -----------------------------------------------------------------------------
# Tests (optional)
# -----------------------------------------------------------------------------

if(EDGE_BUILD_TESTS)
    enable_testing()

    # Catch2 for testing
    find_package(Catch2 3 QUIET)
    if(NOT Catch2_FOUND)
        message(STATUS "Catch2 not found, fetching...")
        include(FetchContent)
        FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            # Pinned to the commit SHA of v3.5.2 (supply-chain: mutable tags can move).
            GIT_TAG 05e10dfccc28c7f973727c54f850237d07d5e10f # v3.5.2
            GIT_SHALLOW FALSE
        )
        FetchContent_MakeAvailable(Catch2)
    endif()

    add_executable(edge-snapshot-tests
        tests/test_main.cpp
        tests/test_types.cpp
        tests/test_config.cpp
        tests/test_probes.cpp
        tests/test_aggregator.cpp
        tests/test_atomic_file.cpp
        tests/test_writer.cpp
        tests/test_json.cpp
        tests/test_journal.cpp
        tests/test_ethtool_link.cpp
        tests/test_serialization_safety.cpp
        tests/test_daemon_lifecycle.cpp
        # daemon.cpp is not part of edge-snapshot-core (it lives in the
        # executable); compile it in so the lifecycle test can reach the
        # detail:: shutdown/watchdog helpers.
        src/daemon.cpp
    )

    target_link_libraries(edge-snapshot-tests
        PRIVATE
            edge-snapshot-core
            Catch2::Catch2WithMain
    )

    include(CTest)
    include(Catch)
    catch_discover_tests(edge-snapshot-tests)
endif()

# -----------------------------------------------------------------------------
# Web UI (optional)
# -----------------------------------------------------------------------------

if(EDGE_WEB_UI)
    add_subdirectory(web)
endif()

# -----------------------------------------------------------------------------
# Installation
# -----------------------------------------------------------------------------

include(GNUInstallDirs)

install(TARGETS edge-healthd
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY inc/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/edge
)

# Systemd service file
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/systemd/edge-healthd.service
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system
)

# Default config
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/config/healthd.conf.example
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/edge
    RENAME healthd.conf.example
)

# D-Bus system bus policy (allows edgehealth user to own edge.health)
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/config/edge-healthd-dbus.conf
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/system.d
)

# -----------------------------------------------------------------------------
# D-Bus code generation (optional — only available in native builds)
#
# Regenerates inc/generated/HealthManagerAdaptor.hpp from the IDL XML.
# The generated file is committed to the repo for cross-compilation builds.
# Usage: cmake --build build --target edge-dbus-generate
# -----------------------------------------------------------------------------

if(NOT CMAKE_CROSSCOMPILING)
    if(EDGE_FETCH_SDBUSCPP AND TARGET sdbus-c++-xml2cpp)
        set(_XML2CPP_CMD $<TARGET_FILE:sdbus-c++-xml2cpp>)
    else()
        find_program(_XML2CPP_CMD sdbus-c++-xml2cpp)
    endif()

    if(_XML2CPP_CMD)
        add_custom_target(edge-dbus-generate
            COMMAND ${_XML2CPP_CMD}
                    ${CMAKE_CURRENT_SOURCE_DIR}/dbus/edge-health-manager.xml
                    --adaptor=${CMAKE_CURRENT_SOURCE_DIR}/inc/generated/HealthManagerAdaptor.hpp
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/dbus/edge-health-manager.xml
            COMMENT "Regenerating D-Bus adaptor from dbus/edge-health-manager.xml"
            VERBATIM
        )
    endif()
endif()

# -----------------------------------------------------------------------------
# CPack (for packaging)
# -----------------------------------------------------------------------------

set(CPACK_PACKAGE_NAME "edge-healthd")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
set(CPACK_PACKAGE_VENDOR "Edge Platform")
set(CPACK_GENERATOR "TGZ")
include(CPack)

# -----------------------------------------------------------------------------
# Summary
# -----------------------------------------------------------------------------

message(STATUS "")
message(STATUS "edge-healthd ${PROJECT_VERSION}")
message(STATUS "  C++ Standard:    C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type:      ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler:        ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  libsystemd:      ${EDGE_HAS_SYSTEMD}")
message(STATUS "  Build tests:     ${EDGE_BUILD_TESTS}")
message(STATUS "  Sanitizers:      ${EDGE_ENABLE_SANITIZERS}")
message(STATUS "  LTO:             ${EDGE_ENABLE_LTO}")
message(STATUS "  Web UI:          ${EDGE_WEB_UI}")
message(STATUS "")
