add_library(libsdb libsdb.cpp)
add_library(sdb::libsdb ALIAS libsdb)

set_target_properties(
    libsdb
    PROPERTIES OUTPUT_NAME sdb
)

target_compile_features(libsdb PUBLIC cxx_std_17)

target_include_directories(
    libsdb
    PRIVATE include
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)
