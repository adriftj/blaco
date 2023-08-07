file(GLOB sources *.c *.cpp *.h *.hpp net/*.c net/*.cpp)
file(GLOB_RECURSE os_depend_sources os/${osname}/*.cpp os/${osname}/*.c)
file(GLOB_RECURSE header_files ${BLACO_INC_DIR}/*.h  ${BLACO_INC_DIR}/*.hpp)

add_library(blaco STATIC ${header_files} ${sources} ${os_depend_sources})
