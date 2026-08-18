add_executable(OwlFoundationTests)

target_sources(OwlFoundationTests PRIVATE
    AssertTests.cpp
    BuildInfoTests.cpp
    CommandLineTests.cpp
    LogTests.cpp
)

target_link_libraries(OwlFoundationTests PRIVATE
    Catch2::Catch2WithMain
    OwlFoundation
)

owl_enable_warnings(OwlFoundationTests)
catch_discover_tests(OwlFoundationTests)
