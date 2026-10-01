add_executable(bench_downloader
    bench_downloader.cpp
)
target_link_libraries(bench_downloader
    PRIVATE dlm_core
    PRIVATE dlm_warnings
)

# Reconstruction of the OLD static algorithm (before ChunkQueue) — only
# used to reproduce the before/after comparison in the README, not used
# anywhere in production code.
add_executable(old_static_bench
    old_static_bench.cpp
)
target_link_libraries(old_static_bench
    PRIVATE dlm_core
    PRIVATE dlm_warnings
)
