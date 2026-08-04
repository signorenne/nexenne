# Example programs: one runnable demo per public component. Built when
# NEXENNE_BUILD_EXAMPLES is ON (default for a top-level build).
#
# Driven by the modules the root actually enabled, so turning one off with
# -DNEXENNE_MODULE_<NAME>=OFF drops its examples instead of failing generation
# on a dangling nexenne::<name> alias.
get_property(_nexenne_enabled_modules GLOBAL PROPERTY NEXENNE_MODULES)
foreach(_module IN LISTS _nexenne_enabled_modules)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_module}/CMakeLists.txt")
        add_subdirectory(${_module})
    endif()
endforeach()
