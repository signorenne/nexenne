include_guard(GLOBAL)

#
# Ensures the doctest target is available. Called from the root build when
# tests are enabled, and from any module's tests subdir built standalone.
# Tries find_package first so a system/conan/vcpkg install is picked up; falls
# back to FetchContent when missing.
#
if(NOT TARGET doctest::doctest_with_main)
    set(NEXENNE_DOCTEST_GIT_TAG "v2.4.11"
        CACHE STRING "doctest release tag to fetch if no system install is found")

    find_package(doctest QUIET CONFIG)

    if(NOT doctest_FOUND)
        message(STATUS "[nexenne] doctest not found on system, fetching ${NEXENNE_DOCTEST_GIT_TAG}")
        include(FetchContent)
        # doctest releases pre-2.4.12 declare cmake_minimum_required(VERSION 3.0),
        # which CMake 4.x rejects. Force a compatible policy version for the
        # fetched sub-build only.
        set(_prev_policy_min "${CMAKE_POLICY_VERSION_MINIMUM}")
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
        FetchContent_Declare(
            doctest
            GIT_REPOSITORY https://github.com/doctest/doctest.git
            GIT_TAG        ${NEXENNE_DOCTEST_GIT_TAG}
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(doctest)
        if(_prev_policy_min)
            set(CMAKE_POLICY_VERSION_MINIMUM "${_prev_policy_min}" CACHE STRING "" FORCE)
        else()
            unset(CMAKE_POLICY_VERSION_MINIMUM CACHE)
        endif()
    endif()
endif()
