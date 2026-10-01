add_executable(bench_downloader
    bench_downloader.cpp
)
target_link_libraries(bench_downloader
    PRIVATE dlm_core
    PRIVATE dlm_warnings
)

# Реконструкция СТАРОГО (досконструкции ChunkQueue) статического алгоритма —
# только для воспроизведения сравнения "до/после" в README, не используется
# нигде в продакшен-коде.
add_executable(old_static_bench
    old_static_bench.cpp
)
target_link_libraries(old_static_bench
    PRIVATE dlm_core
    PRIVATE dlm_warnings
)
