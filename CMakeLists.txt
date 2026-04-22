cmake_minimum_required(VERSION 3.24)
project(Spectr VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Pulp 0.33.0 REQUIRED)

# Tests
include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
)
FetchContent_MakeAvailable(Catch2)
include(CTest)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

add_executable(Spectr-test test_spectr.cpp)
target_link_libraries(Spectr-test PRIVATE Pulp::format Catch2::Catch2WithMain)
target_include_directories(Spectr-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
catch_discover_tests(Spectr-test)

pulp_add_plugin(Spectr
    FORMATS         VST3 AU CLAP Standalone
    PLUGIN_NAME     "Spectr"
    BUNDLE_ID       "com.pulp.spectr"
    MANUFACTURER    "Pulp"
    VERSION         "1.0.0"
    CATEGORY        Effect
    PLUGIN_CODE     "Spec"
    MANUFACTURER_CODE "Pulp"
    AAX_PRODUCT_CODE "SpeP"
    AAX_NATIVE_CODE "Spec"
)
