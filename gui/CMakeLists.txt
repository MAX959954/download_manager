# Stage 7: a thin Qt Widgets layer on top of DownloadManager's public API.
# Only built when DLM_BUILD_GUI is on — keeps Qt out of the default
# build/CI, which never needs it. Qt comes either from vcpkg ("gui" preset)
# or from an installed Qt via CMAKE_PREFIX_PATH ("qt" preset).
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(dlm_gui
    main.cpp
    MainWindow.cpp
    MainWindow.hpp
    DownloadTableModel.cpp
    DownloadTableModel.hpp
)

set_target_properties(dlm_gui PROPERTIES
    AUTOMOC ON
    OUTPUT_NAME "download_manager_gui"
)

target_link_libraries(dlm_gui
    PRIVATE dlm_core
    PRIVATE dlm_warnings
    PRIVATE Qt6::Widgets
)

if (WIN32)
    set_target_properties(dlm_gui PROPERTIES WIN32_EXECUTABLE TRUE)

    # Copy Qt DLLs/plugins next to the exe so it runs straight from the
    # build dir (vcpkg's applocal step only handles vcpkg-provided DLLs).
    if (TARGET Qt6::windeployqt)
        add_custom_command(TARGET dlm_gui POST_BUILD
            COMMAND Qt6::windeployqt --no-translations
                    "$<TARGET_FILE:dlm_gui>"
            COMMENT "Deploying Qt runtime for dlm_gui"
            VERBATIM
        )
    endif()
endif()
