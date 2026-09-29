find_package(CURL CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_library(dlm_core STATIC
    src/Version.cpp
    src/CurlHttpClient.cpp
    src/Downloader.cpp
    src/FileWriter.cpp
    src/ThreadPool.cpp
    src/MetaFile.cpp
)

target_include_directories(dlm_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dlm_core
    PUBLIC  CURL::libcurl
    PUBLIC  Threads::Threads
    PRIVATE dlm_warnings
)
