include_guard(GLOBAL)

#
# Provisions the doctest test framework so a fresh clone always has it, with no
# manual setup. It is fetched from the upstream release archive as a
# hash-verified URL tarball (not a git clone), which keeps the download
# reproducible and free of any dependency on a system install, a CMake package
# registry, or the caller's git environment. The TARGET guard simply skips the
# work when a parent project already provides doctest.
#
if(NOT TARGET doctest::doctest_with_main)
    set(NEXENNE_DOCTEST_VERSION "2.4.12"
        CACHE STRING "doctest release version to fetch")
    set(NEXENNE_DOCTEST_SHA256
        "73381c7aa4dee704bd935609668cf41880ea7f19fa0504a200e13b74999c2d70"
        CACHE STRING "SHA256 of the doctest v${NEXENNE_DOCTEST_VERSION} release tarball")

    message(STATUS "[nexenne] fetching doctest ${NEXENNE_DOCTEST_VERSION} (release tarball)")
    include(FetchContent)
    # doctest 2.4.12 declares cmake_minimum_required(VERSION 3.5). Raise the
    # effective policy floor to 3.10 for its add_subdirectory so CMake 4.x does
    # not warn that compatibility with CMake < 3.10 is going away, then restore
    # the caller's value.
    set(_nexenne_prev_policy_min "${CMAKE_POLICY_VERSION_MINIMUM}")
    set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
    FetchContent_Declare(
        doctest
        URL "https://github.com/doctest/doctest/archive/refs/tags/v${NEXENNE_DOCTEST_VERSION}.tar.gz"
        URL_HASH "SHA256=${NEXENNE_DOCTEST_SHA256}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(doctest)
    set(CMAKE_POLICY_VERSION_MINIMUM "${_nexenne_prev_policy_min}")
endif()
