cmake_minimum_required(VERSION 3.23)

# Standalone-buildable: when this module is the top of the build, bootstrap
# the umbrella helpers and pull in any dependencies listed in module.deps.
# Inside the monorepo build the root has already done this, so it is skipped.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/nexenne_version.cmake")
    nexenne_get_module_version(geometry _nexenne_module_version)
    project(nexenne_geometry
        VERSION     ${_nexenne_module_version}
        DESCRIPTION "nexenne::geometry - geometric primitives and spatial queries"
        LANGUAGES   CXX
    )
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
    include(nexenne_modules)
    nexenne_standalone_bootstrap()
endif()

nexenne_add_module(geometry
    HEADERS
        include/nexenne/geometry/aabb.hpp
        include/nexenne/geometry/capsule.hpp
        include/nexenne/geometry/circle.hpp
        include/nexenne/geometry/error.hpp
        include/nexenne/geometry/frustum.hpp
        include/nexenne/geometry/obb.hpp
        include/nexenne/geometry/plane.hpp
        include/nexenne/geometry/polygon.hpp
        include/nexenne/geometry/ray.hpp
        include/nexenne/geometry/segment.hpp
        include/nexenne/geometry/sphere.hpp
        include/nexenne/geometry/triangle.hpp
        include/nexenne/geometry/geometry.hpp
)

if(NEXENNE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(NEXENNE_BUILD_DOCS)
    add_subdirectory(doc)
endif()
