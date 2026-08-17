add_library(OwlWarnings INTERFACE)

if(MSVC)
    target_compile_options(OwlWarnings INTERFACE
        /W4
        /permissive-
        /Zc:__cplusplus
        /EHsc
    )

    if(OWL_WARNINGS_AS_ERRORS)
        target_compile_options(OwlWarnings INTERFACE /WX)
    endif()
else()
    target_compile_options(OwlWarnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
    )

    if(OWL_WARNINGS_AS_ERRORS)
        target_compile_options(OwlWarnings INTERFACE -Werror)
    endif()
endif()

function(owl_enable_warnings target_name)
    target_link_libraries(${target_name} PRIVATE OwlWarnings)
endfunction()
