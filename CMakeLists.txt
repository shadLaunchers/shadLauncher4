# SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.24)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

# Use the passed version from GitHub Actions if provided
if(DEFINED APP_VERSION)
    set(APP_VERSION ${APP_VERSION})
else()
    set(APP_VERSION "0.0.1")
endif()

message(STATUS "Building shadLauncher4 version: ${APP_VERSION}")

string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" NUMERIC_VERSION "${APP_VERSION}")
project(shadLauncher4 VERSION ${NUMERIC_VERSION} LANGUAGES CXX)

if(WIN32 AND NOT CMAKE_PREFIX_PATH)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/DetectQtInstallation.cmake")
endif ()

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent LinguistTools Xml Network Multimedia)
qt_standard_project_setup()
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

set(QT_TRANSLATIONS "${PROJECT_SOURCE_DIR}/src/qt_ui/translations")
file(GLOB_RECURSE TRANSLATIONS_TS ${QT_TRANSLATIONS}/*.ts)

set_source_files_properties(${TRANSLATIONS_TS} PROPERTIES OUTPUT_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/translations")
qt_add_translation(TRANSLATIONS_QM ${TRANSLATIONS_TS})

set(TRANSLATIONS_QRC ${CMAKE_CURRENT_BINARY_DIR}/translations/translations.qrc)
file(WRITE ${TRANSLATIONS_QRC} "<RCC><qresource prefix=\"translations\">\n")
foreach (QM ${TRANSLATIONS_QM})
  get_filename_component(QM_FILE ${QM} NAME)
  file(APPEND ${TRANSLATIONS_QRC} "<file>${QM_FILE}</file>\n")
endforeach (QM)
file(APPEND ${TRANSLATIONS_QRC} "</qresource></RCC>")

qt_add_resources(TRANSLATIONS ${TRANSLATIONS_QRC})

add_subdirectory(externals)
include_directories(src)

qt_add_resources(RESOURCE_FILES src/shadLauncher4.qrc)

set(COMMON src/common/types.h
           src/common/fs_util.cpp
           src/common/fs_util.h
           src/common/lf_queue.h
           src/common/endian.h
           src/common/io_file.cpp
           src/common/io_file.h
           src/common/alignment.h
           src/common/ntapi.cpp
           src/common/ntapi.h
           src/common/concepts.h
           src/common/enum.h
           src/common/singleton.h
           src/common/path_util.cpp
           src/common/path_util.h
           src/common/versions.cpp
           src/common/versions.h
           src/common/key_manager.cpp
           src/common/key_manager.h
           src/common/logging/backend.cpp
           src/common/logging/backend.h
           src/common/logging/filter.cpp
           src/common/logging/filter.h
           src/common/logging/formatter.h
           src/common/logging/log_entry.h
           src/common/logging/log.h
           src/common/logging/text_formatter.cpp
           src/common/logging/text_formatter.h
           src/common/logging/types.h
           src/common/assert.cpp
           src/common/assert.h
           src/common/arch.h
           src/common/bounded_threadsafe_queue.h
           src/common/polyfill_thread.h
           src/common/string_util.cpp
           src/common/string_util.h
           src/common/thread.cpp
           src/common/thread.h
           src/common/error.cpp
           src/common/error.h
           src/common/memory_patcher.cpp
           src/common/memory_patcher.h
           src/common/crypto.cpp
           src/common/crypto.h
           src/common/picosha2.h
           src/common/zip_util.cpp
           src/common/zip_util.h
)

set(CORE src/core/user_manager.cpp
         src/core/user_manager.h
          src/core/emulator_settings.cpp
          src/core/emulator_settings.h
          src/core/ipc/ipc_client.cpp
          src/core/ipc/ipc_client.h
          src/core/emulator_state.cpp
          src/core/emulator_state.h
          src/core/ipc/ipc_client.cpp
          src/core/ipc/ipc_client.h
          src/core/loader.cpp
          src/core/loader.h
)

set(FILEFORMAT src/core/file_format/psf.cpp
               src/core/file_format/psf.h
               src/core/file_format/npbind.cpp
               src/core/file_format/npbind.h
               src/core/file_format/pfs.h
               src/core/file_format/pkg.cpp
               src/core/file_format/pkg.h
               src/core/file_format/pkg_type.cpp
               src/core/file_format/pkg_type.h
               src/core/file_format/trp.cpp
               src/core/file_format/trp.h
)

set(LIBRARIES src/core/libraries/system/system_service.h
)

set(QT_UI src/qt_ui/settings.cpp
          src/qt_ui/settings.h
          src/qt_ui/gui_save.h
          src/qt_ui/gui_settings.cpp
          src/qt_ui/gui_settings.h
          src/qt_ui/persistent_settings.cpp
          src/qt_ui/persistent_settings.h
          src/qt_ui/gui_application.cpp
          src/qt_ui/gui_application.h
          src/qt_ui/main_window.cpp
          src/qt_ui/main_window.h
          src/qt_ui/main_window.ui
          src/qt_ui/custom_dock_widget.h
          src/qt_ui/game_item_base.h
          src/qt_ui/game_item_base.cpp
          src/qt_ui/game_item.h
          src/qt_ui/game_item.cpp
          src/qt_ui/custom_table_widget_item.cpp
          src/qt_ui/custom_table_widget_item.h
          src/qt_ui/table_item_delegate.cpp
          src/qt_ui/table_item_delegate.h
          src/qt_ui/gui_game_info.h
          src/qt_ui/game_compatibility.cpp
          src/qt_ui/game_compatibility.h
          src/qt_ui/game_list_delegate.cpp
          src/qt_ui/game_list_delegate.h
          src/qt_ui/flow_layout.cpp
          src/qt_ui/flow_layout.h
          src/qt_ui/flow_widget.cpp
          src/qt_ui/flow_widget.h
          src/qt_ui/flow_widget_item.cpp
          src/qt_ui/flow_widget_item.h
          src/qt_ui/game_list_base.cpp
          src/qt_ui/game_list_base.h
          src/qt_ui/game_list.cpp
          src/qt_ui/game_list.h
          src/qt_ui/game_list_grid_item.cpp
          src/qt_ui/game_list_grid_item.h
          src/qt_ui/game_list_grid.cpp
          src/qt_ui/game_list_grid.h
          src/qt_ui/qt_utils.cpp
          src/qt_ui/qt_utils.h
          src/qt_ui/game_list_table.cpp
          src/qt_ui/game_list_table.h
          src/qt_ui/game_list_frame.cpp
          src/qt_ui/game_list_frame.h
          src/qt_ui/stylesheets.h
          src/qt_ui/progress_dialog.cpp
          src/qt_ui/progress_dialog.h
          src/qt_ui/progress_indicator.cpp
          src/qt_ui/progress_indicator.h
          src/qt_ui/localized.cpp
          src/qt_ui/localized.h
          src/qt_ui/change_log_dialog.cpp
          src/qt_ui/change_log_dialog.h
          src/qt_ui/game_install_dialog.cpp
          src/qt_ui/game_install_dialog.h
          src/qt_ui/downloader.cpp
          src/qt_ui/downloader.h
          src/qt_ui/user_manager_dialog.cpp
          src/qt_ui/user_manager_dialog.h
          src/qt_ui/game_list_exporter.cpp
          src/qt_ui/game_list_exporter.h
          src/qt_ui/sfo_viewer_dialog.cpp
          src/qt_ui/sfo_viewer_dialog.h
          src/qt_ui/sfo_model.cpp
          src/qt_ui/sfo_model.h
          src/qt_ui/sfo_key_map.h
          src/qt_ui/settings_dialog.cpp
          src/qt_ui/settings_dialog.h
          src/qt_ui/settings_dialog.ui
          src/qt_ui/settings_dialog_helper_texts.cpp
          src/qt_ui/settings_dialog_helper_texts.h
          src/qt_ui/version_dialog.cpp
          src/qt_ui/version_dialog.h
          src/qt_ui/version_dialog.ui
          src/qt_ui/npbind_dialog.cpp
          src/qt_ui/npbind_dialog.h
          src/qt_ui/crypto_key_dialog.cpp
          src/qt_ui/crypto_key_dialog.h
          src/qt_ui/cheats_patches_dialog.cpp
          src/qt_ui/cheats_patches_dialog.h
          src/qt_ui/cheats_patches_dialog.ui
	  src/qt_ui/background_music_player.cpp
          src/qt_ui/background_music_player.h
          src/qt_ui/cheats_patches_repository_config.h
          src/qt_ui/hex_plain_text_edit.h
	  src/qt_ui/log_presets_dialog.cpp
          src/qt_ui/log_presets_dialog.h
          src/qt_ui/pkg_install_dir_select_dialog.cpp
          src/qt_ui/pkg_install_dir_select_dialog.h
          src/qt_ui/pkg_install_model.cpp
          src/qt_ui/pkg_install_model.h
          src/qt_ui/centered_checkbox_delegate.h
)

set(SOURCES ${COMMON} ${CORE} ${QT_UI} ${FILEFORMAT} ${LIBRARIES} ${TRANSLATIONS} src/game_info.h src/main.cpp)
qt_add_executable(shadLauncher4 ${SOURCES})

# Automatically generate a version header with the same version
configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/version.h.in
    ${CMAKE_BINARY_DIR}/generated/version.h
    @ONLY
)
target_include_directories(shadLauncher4 PRIVATE ${CMAKE_BINARY_DIR}/generated)

target_link_libraries(shadLauncher4 PRIVATE fmt::fmt Qt6::Widgets Qt6::Concurrent Qt6::Xml Qt6::Network Qt6::Multimedia nlohmann_json::nlohmann_json ntdll SDL3::SDL3 libdeflate_static vulkaninfowin)

target_sources(shadLauncher4 PRIVATE src/shadLauncher4.rc)
target_sources(shadLauncher4 PRIVATE ${RESOURCE_FILES})

if (WIN32)
    target_link_libraries(shadLauncher4 PRIVATE mincore bcrypt)
    if (MSVC)
        # MSVC likes putting opinions on what people can use, disable:
        add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE -D_SCL_SECURE_NO_WARNINGS)
    endif()

    add_definitions(-DNOMINMAX -DWIN32_LEAN_AND_MEAN)

    if (MSVC)
        # Needed for conflicts with time.h of windows.h
        add_definitions(-D_TIMESPEC_DEFINED)
    endif()

    # Target Windows 10 RS5
    add_definitions(-DNTDDI_VERSION=0x0A000006 -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00)

    if (MSVC)
        target_link_libraries(shadLauncher4 PRIVATE clang_rt.builtins-x86_64.lib)
    endif()
endif()

set_target_properties(shadLauncher4 PROPERTIES
   WIN32_EXECUTABLE ON
#   MACOSX_BUNDLE ON
#   MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/dist/MacOSBundleInfo.plist.in"
#   MACOSX_BUNDLE_ICON_FILE "shadPS4.icns"
#   MACOSX_BUNDLE_SHORT_VERSION_STRING "${APP_VERSION}"
)
