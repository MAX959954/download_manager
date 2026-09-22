add_executable(download_manager
    main.cpp
)

target_link_libraries(download_manager
    PRIVATE dlm_core
    PRIVATE dlm_warnings
)

set_target_properties(download_manager PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

include(GNUInstallDirs)
install(TARGETS download_manager
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
