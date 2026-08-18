add_executable(OwlFoundationTests)

target_sources(OwlFoundationTests PRIVATE
    BuildInfoTests.cpp
    CommandLineTests.cpp
)

target_link_libraries(OwlFoundationTests PRIVATE
    Catch2::Catch2WithMain
    OwlFoundation
)

owl_enable_warnings(OwlFoundationTests)
catch_discover_tests(OwlFoundationTests)
