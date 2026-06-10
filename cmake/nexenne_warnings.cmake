include_guard(GLOBAL)

#
# Build-only INTERFACE target collecting compiler warnings.
# Link with: target_link_libraries(<tgt> PRIVATE nexenne::warnings)
#
# Not exported. Tests and examples opt in; the header-only libs themselves
# never link it (warnings apply to translation units, not header-only deps).
#
add_library(nexenne_warnings INTERFACE)
add_library(nexenne::warnings ALIAS nexenne_warnings)

if(MSVC)
    target_compile_options(nexenne_warnings INTERFACE
        /W4
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
    )
    if(NEXENNE_WARNINGS_AS_ERRORS)
        target_compile_options(nexenne_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(nexenne_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_compile_options(nexenne_warnings INTERFACE
            -Wmisleading-indentation
            -Wduplicated-cond
            -Wduplicated-branches
            -Wlogical-op
            -Wuseless-cast
        )
    endif()
    if(NEXENNE_WARNINGS_AS_ERRORS)
        target_compile_options(nexenne_warnings INTERFACE -Werror)
    endif()
endif()
