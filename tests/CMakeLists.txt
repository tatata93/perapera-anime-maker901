find_package(Catch2 3 CONFIG REQUIRED)

add_executable(perapera_core_tests
    CoreModelTests.cpp
    CommandStackTests.cpp
)

target_link_libraries(perapera_core_tests PRIVATE
    perapera_core
    Catch2::Catch2WithMain
)

include(CTest)
include(Catch)
catch_discover_tests(perapera_core_tests)
