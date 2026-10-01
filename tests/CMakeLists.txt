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

add_executable(test_parallel_downloader
    test_parallel_downloader.cpp
)


target_include_directories(test_parallel_downloader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_parallel_downloader PRIVATE dlm_core PRIVATE dlm_warnings)
add_test(NAME test_parallel_downloader COMMAND test_parallel_downloader)


add_executable(test_resumable_downloader
    test_resumable_downloader.cpp
)
target_include_directories(test_resumable_downloader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_resumable_downloader PRIVATE dlm_core PRIVATE dlm_warnings)
add_test(NAME test_resumable_downloader COMMAND test_resumable_downloader)

add_executable(test_pause_cancel_resumable
    test_pause_cancel_resumable.cpp
)
target_include_directories(test_pause_cancel_resumable PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_pause_cancel_resumable PRIVATE dlm_core PRIVATE dlm_warnings)
add_test(NAME test_pause_cancel_resumable COMMAND test_pause_cancel_resumable)

add_executable(test_sha256
    test_sha256.cpp
)
target_include_directories(test_sha256 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_sha256 PRIVATE dlm_core PRIVATE dlm_warnings)
add_test(NAME test_sha256 COMMAND test_sha256)

add_executable(test_rate_limiter
    test_rate_limiter.cpp
)
target_include_directories(test_rate_limiter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_rate_limiter PRIVATE dlm_core PRIVATE dlm_warnings)
add_test(NAME test_rate_limiter COMMAND test_rate_limiter)

add_executable(test_download_manager
    test_download_manager.cpp
)
target_include_directories(test_download_manager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_download_manager PRIVATE dlm_core PRIVATE dlm_warnings)
add_test(NAME test_download_manager COMMAND test_download_manager)

# --- Этап 8: собственный HTTP+TLS-клиент на сырых сокетах ---
# Чистые юнит-тесты разбора URL/заголовков/chunked-кодирования — без сети,
# гоняются в обычном ctest вместе со всеми остальными.

add_executable(test_url_parsing
    test_url_parsing.cpp
)
target_include_directories(test_url_parsing PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_url_parsing PRIVATE dlm_core PRIVATE dlm_warnings)
add_test(NAME test_url_parsing COMMAND test_url_parsing)

add_executable(test_http_header_parsing
    test_http_header_parsing.cpp
)
target_include_directories(test_http_header_parsing PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_http_header_parsing PRIVATE dlm_core PRIVATE dlm_warnings)
add_test(NAME test_http_header_parsing COMMAND test_http_header_parsing)

add_executable(test_chunked_decoder
    test_chunked_decoder.cpp
)
target_include_directories(test_chunked_decoder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_chunked_decoder PRIVATE dlm_core PRIVATE dlm_warnings)
add_test(NAME test_chunked_decoder COMMAND test_chunked_decoder)

# --- "_live" тесты ниже реально ходят в интернет (GitHub) и проверяют
# SocketHttpClient/CurlHttpClient на настоящем TLS-сервере. Собираются и
# регистрируются в ctest как обычно, но держи в голове, что они не пройдут
# без сети (например, в офлайн-CI) — это осознанный компромисс ради
# честной end-to-end проверки TLS-рукопожатия, которую никакой фейк не даст.

add_executable(test_socket_http_client_live
    test_socket_http_client_live.cpp
)
target_include_directories(test_socket_http_client_live PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_socket_http_client_live PRIVATE dlm_core PRIVATE dlm_warnings)
add_test(NAME test_socket_http_client_live COMMAND test_socket_http_client_live)
set_tests_properties(test_socket_http_client_live PROPERTIES LABELS "live")

add_executable(test_downloader_live
    test_downloader_live.cpp
)
target_include_directories(test_downloader_live PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_downloader_live PRIVATE dlm_core PRIVATE dlm_warnings)
add_test(NAME test_downloader_live COMMAND test_downloader_live)
set_tests_properties(test_downloader_live PROPERTIES LABELS "live")

add_executable(test_curl_http_client_live
    test_curl_http_client_live.cpp
)
target_include_directories(test_curl_http_client_live PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_curl_http_client_live PRIVATE dlm_core PRIVATE dlm_warnings)
add_test(NAME test_curl_http_client_live COMMAND test_curl_http_client_live)
set_tests_properties(test_curl_http_client_live PROPERTIES LABELS "live")