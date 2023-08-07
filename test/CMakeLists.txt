include_directories(${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE blaco_test_sources *.cpp)

add_executable(blaco_test ${blaco_test_sources})
target_link_libraries(blaco_test blaco ${OSLIBS})
