add_executable(test_downloader
    test_downloader.cpp
)

target_link_libraries(test_downloader
    PRIVATE dlm_core
    PRIVATE dlm_warnings
)

add_test(NAME test_downloader COMMAND test_downloader)

add_executable(test_chunked_downloader
    test_chunked_downloader.cpp
)

target_include_directories(test_chunked_downloader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_chunked_downloader PRIVATE dlm_core PRIVATE dlm_warnings)
add_test(NAME test_chunked_downloader COMMAND test_chunked_downloader)
