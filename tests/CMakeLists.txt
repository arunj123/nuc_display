include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
# For Windows: do not change the default runtime library
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Common test include directories
set(TEST_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/src
)

# --- ThreadPool Tests ---
add_executable(test_utils test_utils.cpp)
target_include_directories(test_utils PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(test_utils GTest::gtest_main)
add_test(NAME UtilsTests COMMAND test_utils)

# --- Module Tests ---
# Note: Testing modules that depend on hardware (DRM, VA-API) is tricky.
# We will focus on logic and state for now.
add_executable(test_modules test_modules.cpp ../src/modules/image_loader.cpp ../src/modules/text_renderer.cpp)
target_include_directories(test_modules PRIVATE ${TEST_INCLUDE_DIRS})
target_link_libraries(test_modules 
    GTest::gtest_main
    ${FREETYPE_LIBRARIES}
    ${HARFBUZZ_LIBRARIES}
    ${LIBJPEG_LIBRARIES}
    ${LIBPNG_LIBRARIES}
)
add_test(NAME ModuleTests COMMAND test_modules)
