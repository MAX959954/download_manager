find_package(CURL CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(dlm_core STATIC
    src/Version.cpp
    src/CurlHttpClient.cpp
    src/SocketHttpClient.cpp
    src/Downloader.cpp
    src/FileWriter.cpp
    src/ThreadPool.cpp
    src/MetaFile.cpp
    src/Sha256.cpp
    src/DownloadManager.cpp
    src/Url.cpp
    src/HttpHeaderParsing.cpp
    src/ChunkedDecoder.cpp
)

target_include_directories(dlm_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dlm_core
    PUBLIC  CURL::libcurl
    PUBLIC  Threads::Threads
    PUBLIC  OpenSSL::SSL
    PUBLIC  OpenSSL::Crypto
    PRIVATE dlm_warnings
)

if(WIN32)
    # SocketHttpClient.cpp использует Winsock (socket/connect/getaddrinfo).
    target_link_libraries(dlm_core PUBLIC ws2_32)
endif()
