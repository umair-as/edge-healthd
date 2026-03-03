# -----------------------------------------------------------------------------
# Edge-Healthd Web UI
# -----------------------------------------------------------------------------
#
# This builds the Go microserver with embedded frontend assets.
#
# Usage:
#   cmake -B build -DEDGE_WEB_UI=ON
#   cmake --build build
#
# Cross-compile:
#   cmake -B build -DEDGE_WEB_UI=ON -DEDGE_WEB_GOARCH=arm64
#   cmake -B build -DEDGE_WEB_UI=ON -DEDGE_WEB_GOARCH=riscv64
# -----------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.20)

# -----------------------------------------------------------------------------
# Build Options
# -----------------------------------------------------------------------------

set(EDGE_WEB_GOOS "linux" CACHE STRING "Go target OS (GOOS)")
set(EDGE_WEB_GOARCH "" CACHE STRING "Go target architecture (GOARCH). Empty = native.")

# -----------------------------------------------------------------------------
# Find Go
# -----------------------------------------------------------------------------

find_program(GO_EXECUTABLE go REQUIRED)

execute_process(
    COMMAND ${GO_EXECUTABLE} version
    OUTPUT_VARIABLE GO_VERSION_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE GO_VERSION_RESULT
)

if(GO_VERSION_RESULT EQUAL 0)
    string(REGEX MATCH "go[0-9]+\\.[0-9]+" GO_VERSION "${GO_VERSION_OUTPUT}")
    message(STATUS "Found Go: ${GO_VERSION}")
else()
    message(FATAL_ERROR "Failed to determine Go version")
endif()

# -----------------------------------------------------------------------------
# Find Node.js / npm
# -----------------------------------------------------------------------------

find_program(NPM_EXECUTABLE npm REQUIRED)
find_program(NODE_EXECUTABLE node REQUIRED)

execute_process(
    COMMAND ${NODE_EXECUTABLE} --version
    OUTPUT_VARIABLE NODE_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "Found Node.js: ${NODE_VERSION}")

execute_process(
    COMMAND ${NPM_EXECUTABLE} --version
    OUTPUT_VARIABLE NPM_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "Found npm: ${NPM_VERSION}")

# -----------------------------------------------------------------------------
# Paths
# -----------------------------------------------------------------------------

set(WEB_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(UI_DIR ${WEB_SOURCE_DIR}/ui)
set(SERVER_DIR ${WEB_SOURCE_DIR}/server)
set(UI_DIST_DIR ${UI_DIR}/dist)
set(SERVER_DIST_DIR ${SERVER_DIR}/dist)

# Output binary
set(WEB_UI_BINARY ${CMAKE_BINARY_DIR}/edge-healthd-ui)

# -----------------------------------------------------------------------------
# Frontend Build Target
# -----------------------------------------------------------------------------

# Install npm dependencies
add_custom_command(
    OUTPUT ${UI_DIR}/node_modules/.package-lock.json
    COMMAND ${NPM_EXECUTABLE} ci
    WORKING_DIRECTORY ${UI_DIR}
    DEPENDS ${UI_DIR}/package.json ${UI_DIR}/package-lock.json
    COMMENT "Installing frontend dependencies"
)

# Build frontend
add_custom_command(
    OUTPUT ${UI_DIST_DIR}/index.html
    COMMAND ${NPM_EXECUTABLE} run build
    WORKING_DIRECTORY ${UI_DIR}
    DEPENDS
        ${UI_DIR}/node_modules/.package-lock.json
        ${UI_DIR}/src/main.tsx
        ${UI_DIR}/src/App.tsx
        ${UI_DIR}/index.html
        ${UI_DIR}/vite.config.ts
        ${UI_DIR}/tailwind.config.js
    COMMENT "Building frontend"
)

add_custom_target(edge-healthd-ui-frontend
    DEPENDS ${UI_DIST_DIR}/index.html
)

# -----------------------------------------------------------------------------
# Copy dist to server for embedding
# -----------------------------------------------------------------------------

add_custom_command(
    OUTPUT ${SERVER_DIST_DIR}/.copied
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${UI_DIST_DIR} ${SERVER_DIST_DIR}
    COMMAND ${CMAKE_COMMAND} -E touch ${SERVER_DIST_DIR}/.copied
    DEPENDS ${UI_DIST_DIR}/index.html
    COMMENT "Copying frontend dist to server"
)

add_custom_target(edge-healthd-ui-copy-dist
    DEPENDS ${SERVER_DIST_DIR}/.copied
)

add_dependencies(edge-healthd-ui-copy-dist edge-healthd-ui-frontend)

# -----------------------------------------------------------------------------
# Go Server Build Target
# -----------------------------------------------------------------------------

# Collect Go source files for dependency tracking
file(GLOB GO_SOURCES ${SERVER_DIR}/*.go)

# Set up cross-compile environment
set(GO_ENV_VARS "CGO_ENABLED=0" "GOOS=${EDGE_WEB_GOOS}")

if(EDGE_WEB_GOARCH)
    list(APPEND GO_ENV_VARS "GOARCH=${EDGE_WEB_GOARCH}")
    message(STATUS "Cross-compiling Go for ${EDGE_WEB_GOOS}/${EDGE_WEB_GOARCH}")
else()
    message(STATUS "Building Go for native architecture")
endif()

# Build Go binary
add_custom_command(
    OUTPUT ${WEB_UI_BINARY}
    COMMAND ${CMAKE_COMMAND} -E env ${GO_ENV_VARS}
        ${GO_EXECUTABLE} build
        -ldflags "-s -w"
        -o ${WEB_UI_BINARY}
        .
    WORKING_DIRECTORY ${SERVER_DIR}
    DEPENDS
        ${GO_SOURCES}
        ${SERVER_DIR}/go.mod
        ${SERVER_DIST_DIR}/.copied
    COMMENT "Building Go server"
)

add_custom_target(edge-healthd-ui ALL
    DEPENDS ${WEB_UI_BINARY}
)

add_dependencies(edge-healthd-ui edge-healthd-ui-copy-dist)

# -----------------------------------------------------------------------------
# Installation
# -----------------------------------------------------------------------------

include(GNUInstallDirs)

install(PROGRAMS ${WEB_UI_BINARY}
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES ${WEB_SOURCE_DIR}/systemd/edge-healthd-ui.service
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system
)

install(PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/gen-tls-cert.sh
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME edge-healthd-gen-cert
)

# -----------------------------------------------------------------------------
# Development Targets
# -----------------------------------------------------------------------------

# Target to run frontend dev server
add_custom_target(edge-healthd-ui-dev
    COMMAND ${NPM_EXECUTABLE} run dev
    WORKING_DIRECTORY ${UI_DIR}
    COMMENT "Starting frontend development server"
)

# Target to run Go server in dev mode
add_custom_target(edge-healthd-ui-server-dev
    COMMAND ${GO_EXECUTABLE} run . -listen :8080 -state /tmp/state.json  # HTTP, no TLS for dev
    WORKING_DIRECTORY ${SERVER_DIR}
    COMMENT "Starting Go server in development mode"
)

# -----------------------------------------------------------------------------
# Summary
# -----------------------------------------------------------------------------

message(STATUS "")
message(STATUS "Web UI Configuration:")
message(STATUS "  Go:              ${GO_VERSION}")
message(STATUS "  Node.js:         ${NODE_VERSION}")
message(STATUS "  npm:             ${NPM_VERSION}")
message(STATUS "  Target OS:       ${EDGE_WEB_GOOS}")
message(STATUS "  Target Arch:     ${EDGE_WEB_GOARCH}")
message(STATUS "  Output:          ${WEB_UI_BINARY}")
message(STATUS "")
