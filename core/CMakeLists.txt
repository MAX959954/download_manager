find_package(CURL CONFIG REQUIRED)

add_library(dlm_core STATIC
    src/Version.cpp
)

target_include_directories(dlm_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dlm_core
    PUBLIC  CURL::libcurl
    PRIVATE dlm_warnings
)