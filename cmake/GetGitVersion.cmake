# cmake/GetGitVersion.cmake
# Derives the project version from git tags at configure time.
#
# Convention: tags must match "v*" (e.g. v0.5.1, v1.0.0).
# Uses `git describe --tags --match "v*" --long --dirty` so the output
# always includes the commit count and hash, not just the tag name.
#
# Sets in parent scope:
#   GIT_DERIVED_VERSION            — plain semver for project(VERSION ...), e.g. "0.5.1"
#   EDGE_HEALTHD_VERSION_DETAILED  — full build string for --version output, e.g.:
#                                      "0.5.1"                    (on exact clean tag)
#                                      "0.5.1-3-abcd123"          (off tag, clean)
#                                      "0.5.1-3-abcd123-dirty"    (uncommitted changes)
#
# Fallback chain (in order):
#   1. git describe (dev checkouts)
#   2. VERSION file in repo root (Yocto / tarball builds without .git)
#   3. "0.0.0" with a CMake warning

find_package(Git QUIET)

set(GIT_DERIVED_VERSION           "0.0.0")
set(EDGE_HEALTHD_VERSION_DETAILED "0.0.0-unknown")

if(Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --match "v*" --long --dirty
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE   _git_describe
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE   _git_result
    )

    if(_git_result EQUAL 0 AND _git_describe MATCHES "^v[0-9]")
        # Extract semver: v0.5.1-3-gabcd123[-dirty] → 0.5.1
        string(REGEX REPLACE "^v([0-9]+\\.[0-9]+\\.[0-9]+)-.*$" "\\1"
               GIT_DERIVED_VERSION "${_git_describe}")

        # Build detailed string: strip leading 'v', remove 'g' prefix from hash
        string(REGEX REPLACE "^v" "" _detailed "${_git_describe}")
        string(REGEX REPLACE "-g([0-9a-f]+)" "-\\1" _detailed "${_detailed}")

        # On an exact clean tag (commits-since == 0, no dirty) collapse to plain semver
        if(_detailed MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+-0-[0-9a-f]+$")
            set(EDGE_HEALTHD_VERSION_DETAILED "${GIT_DERIVED_VERSION}")
        else()
            # Drop the "-0-<hash>" infix when commits-since is 0 but workspace is dirty
            string(REGEX REPLACE "^([0-9]+\\.[0-9]+\\.[0-9]+)-0-[0-9a-f]+(-.+)$" "\\1\\2"
                   _detailed "${_detailed}")
            set(EDGE_HEALTHD_VERSION_DETAILED "${_detailed}")
        endif()
    endif()
endif()

# Fallback: VERSION file present in source tree (Yocto / non-git builds)
if(GIT_DERIVED_VERSION STREQUAL "0.0.0")
    set(_ver_file "${CMAKE_SOURCE_DIR}/VERSION")
    if(EXISTS "${_ver_file}")
        file(READ "${_ver_file}" _ver_content)
        string(STRIP "${_ver_content}" _ver_content)
        if(_ver_content MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
            set(GIT_DERIVED_VERSION           "${_ver_content}")
            set(EDGE_HEALTHD_VERSION_DETAILED "${_ver_content}")
        endif()
    else()
        message(WARNING
            "GetGitVersion: git describe failed and no VERSION file found. "
            "Version will be reported as 0.0.0. "
            "Create a VERSION file containing the release version, e.g.: echo '0.5.1' > VERSION")
    endif()
endif()
