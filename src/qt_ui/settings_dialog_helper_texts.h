// SPDX-FileCopyrightText: Copyright 2025-2026 shadLauncher4 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QObject>
#include <QString>

class SettingsDialogHelperTexts : public QObject {
    Q_OBJECT

public:
    SettingsDialogHelperTexts();

    const struct settings {
        // clang-format off
        //general
        const QString general_show_splash = tr("Show Splash Screen:\\nShows the game's splash screen (a special image) while the game is starting.");
        const QString general_disable_trophy_popup= tr("Disable Trophy Pop-ups:\\nDisable in-game trophy notifications. Trophy progress can still be tracked using the Trophy Viewer (right-click the game in the main window).");
        const QString general_volume_slider = tr("Volume:\\nAdjust volume for games on a global level, range goes from 0-500% with the default being 100%.");
        const QString general_open_custom_trophy_location = tr("Open the custom trophy images/sounds folder:\\nYou can add custom images to the trophies and an audio.\\nAdd the files to custom_trophy with the following names:\\ntrophy.wav OR trophy.mp3, bronze.png, gold.png, platinum.png, silver.png\\nNote: The sound will only work in QT versions.");
        const QString general_discord_rpc = tr("Enable Discord Rich Presence:\\nDisplays the emulator icon and relevant information on your Discord profile.");
        const QString general_microphone = tr("Microphone:\\nNone: Does not use the microphone.\\nDefault Device: Will use the default device defined in the system.\\nOr manually choose the microphone to be used from the list.");
        const QString general_updater = tr("GUI Updates:\\nRelease: Official versions released every month that may be very outdated, but are more reliable and tested.\\nNightly: Development versions that have all the latest features and fixes, but may contain bugs and are less stable.\\n\\n*This update applies only to the Qt user interface. To update the emulator core, please use the 'Version Manager' menu.");
        const QString general_updater_check_startup = tr("Check for Updates at Startup:\\nAutomatically check for a new launcher version each time it starts.");
        const QString general_updater_changelog = tr("Always Show Changelog:\\nDisplay the changelog dialog after installing an update, even for minor releases.");
        const QString general_updater_check_now = tr("Check for Updates:\\nManually check right now for a newer launcher version.");
        const QString general_fps_counter = tr("Show Simple FPS Counter:\\nDisplays a basic frames-per-second counter overlay while a game is running.");
        const QString general_pkg_separate_update = tr("Use Separate Update Folder:\\nInstall PKG game updates to their own folder instead of merging them into the base game's install folder.");
        //paths
        const QString paths_gameDir = tr("Game Folders:\\nThe list of folders to check for installed games.");
        const QString paths_gameDir_add = tr("Add Folder:\\nAdd a new folder to the list of game installation folders.");
        const QString paths_gameDir_remove = tr("Remove Folder:\\nRemove the selected folder from the list of game installation folders.");
        const QString paths_dlcDir = tr("DLC Path:\\nThe folder where game DLC is loaded from.");
        const QString paths_dlcDir_browse = tr("Browse:\\nBrowse for a folder to set as the DLC path.");
        const QString paths_homeDir = tr("Home Folder:\\nThe folder where the emulator stores user data such as save files and trophies.");
        const QString paths_homeDir_browse = tr("Browse:\\nBrowse for a folder to set as the Home folder.");
        const QString paths_sysmodulesDir = tr("System Modules Folder:\\nThe folder where system modules are loaded from.");
        const QString paths_sysmodulesDir_browse = tr("Browse:\\nBrowse for a folder to set as the System Modules folder.");
        const QString paths_fontsDir = tr("System Fonts Folder:\\nThe folder where system fonts are loaded from.");
        const QString paths_fontsDir_browse = tr("Browse:\\nBrowse for a folder to set as the System Fonts folder.");
        //log
        const QString log_filter = tr("Log Filter:\\nFilters the log to only print specific information.\\nExamples: \"Core:Debug\" \"Lib.Pad:Debug Common.Filesystem:Error\" \"*:Critical\"\\nLevels: trace, debug, info, warning, error, critical, off - in this order, a specific level silences all levels preceding it in the list and logs every level after it.");
        const QString log_enable = tr("Enable Logging:\\nEnables logging.\\nDo not change this if you do not know what you're doing!\\nWhen asking for help, make sure this setting is ENABLED.");
        const QString log_open_location = tr("Open Log Location:\\nOpen the folder where the log file is saved.");
        const QString log_separate_files = tr("Separate Log Files:\\nWrites a separate logfile for each game.");
        const QString log_sync = tr("Log Sync:\\nSwitch between sync (order) or async (performance).");
        const QString log_skip_duplicate = tr("Log Skip Duplicate:\\nSave storage by avoiding writing log that is identical.");
        const QString log_max_skip_duration = tr("Log Max Skip Duration:\\nInterval without writing same lines (ms) - only if 'Log Skip Duplicate' enabled.");
        const QString log_size_limit = tr("Log Size Limit:\\nMaximum size of log files (bytes).");
        const QString log_append = tr("Log Append:\\nAppend to existing logs.");
        const QString log_type = tr("Log Type:\\nChoose between wincolor or msvc log types.\\nwincolor: Default logging for Windows\\nmsvc: Logging for debugging");
        const QString log_section = tr("Log:\\nSettings that control what gets logged and how log files are written.");
        const QString log_presets = tr("Load Presets...:\\nChoose from a list of common log filter presets instead of typing one by hand.");
        //gui
        const QString general_scan_depth_combo = tr("Directory Scan Depth:\\nSet the maximum depth when scanning for games in the specified game folders.\\n1 means one level of subfolders is scanned, and so on.");
        const QString gui_background_image = tr("Background Image:\\nControl the opacity of the game background image.");
        const QString gui_music = tr("Play Title Music:\\nIf a game supports it, enable playing special music when selecting the game in the GUI.");
        const QString gui_music_volume = tr("Music Volume:\\nAdjust the volume of the background/title music played in the GUI.");
        const QString gui_theme = tr("Theme:\\nChoose the stylesheet used for the launcher's interface. Drop .qss stylesheet files into the \"themes\" folder inside your user data directory to add custom themes.");
        const QString gui_show_background_image = tr("Show Background Image:\\nDisplay a background image behind the game list. Use the Opacity slider below to control how visible it is.");
        //audio
        const QString audio_backend = tr("Audio Backend:\\nSelects the backend library used for audio output. SDL is recommended for most users.");
        const QString audio_general_device = tr("Audio Device (general):\\nSelects which system audio output device the emulator uses for general game audio.");
        const QString audio_ds4_speaker = tr("Audio Device (DS4 speaker):\\nSelects which system audio output device is used for sound played through a connected DualShock 4/DualSense controller's built-in speaker.");
        //compatibility
        const QString compat_section = tr("Compatibility:\\nSettings for displaying and updating the game compatibility database.");
        const QString compat_check_on_startup = tr("Update Compatibility On Startup:\\nAutomatically update the compatibility database when shadPS4 starts.");
        const QString compat_update_button = tr("Update Compatibility Database:\\nImmediately update the compatibility database.");
        //gpu
        const QString gpu_readback_mode = tr("Readback Mode:\\nControls how the emulator handles GPU readbacks.Enabled them with make some games work better.\\nDisabled is recommended option\\nRelaxed is same as Precised but uses less fence protection,but can give more fps\\nPrecise mode should be used for maximum compatibility where readbacks are neccesary");
        const QString gpu_graphics_adapter = tr("Graphics Device:\\nOn multiple GPU systems, select the GPU the emulator will use from the drop down list,\\nor select \"Auto Select\" to automatically determine it.");
        const QString gpu_present_mode = tr("Present Mode:\\nConfigures how video output will be presented to your screen.\\n\\nMailbox: Frames synchronize with your screen's refresh rate. New frames will replace any pending frames. Reduces latency but may skip frames if running behind.\\nFifo: Frames synchronize with your screen's refresh rate. New frames will be queued behind pending frames. Ensures all frames are presented but may increase latency.\\nImmediate: Frames immediately present to your screen when ready. May result in tearing.");
        const QString gpu_window_size = tr("Width/Height:\\nSets the size of the emulator window at launch, which can be resized during gameplay.\\nThis is different from the in-game resolution.");
        const QString gpu_enable_hdr = tr("Enable HDR:\\nEnables HDR in games that support it.\\nYour monitor must have support for the BT2020 PQ color space and the RGB10A2 swapchain format.");
        const QString gpu_vblank_frequency = tr("Vblank Frequency:\\nThe frame rate at which the emulator refreshes at (60hz is the baseline, whether the game runs at 30 or 60fps). Changing this may have adverse effects, such as increasing the game speed, or breaking critical game functionality that does not expect this to change!");
        const QString gpu_readback_linear_images = tr("Enable Readback Linear Images:\\nEnables async downloading of GPU modified linear images.\\nMight fix issues in some games.");
        const QString gpu_display_options = tr("Display Options:\\nSettings that control how and where the emulator's video output is presented.");
        const QString gpu_display_mode = tr("Display Mode:\\nChoose whether the game runs in a window, fullscreen, or borderless fullscreen.");
        const QString gpu_fsr = tr("FSR Settings:\\nAMD FidelityFX Super Resolution upscaling and sharpening options.");
        const QString gpu_fsr_enable = tr("Enable FSR:\\nUses AMD FidelityFX Super Resolution to upscale the rendered image, which can improve performance at lower internal resolutions.");
        const QString gpu_rcas_enable = tr("Enable RCAS (sharpening):\\nApplies FSR's Robust Contrast Adaptive Sharpening pass to the image. Requires FSR to be enabled.");
        const QString gpu_rcas_attenuation = tr("RCAS Attenuation:\\nControls the strength of the RCAS sharpening effect. Lower values sharpen the image more.");
        //input
        const QString input_hide_cursor = tr("Hide Cursor:\\nChoose when the cursor will disappear:\\nNever: You will always see the mouse.\\nidle: Set a time for it to disappear after being idle.\\nAlways: you will never see the mouse.");
        const QString input_idle_timeout = tr("Hide Idle Cursor Timeout:\\nThe duration (seconds) after which the cursor that has been idle hides itself.");
        const QString input_motion_controls = tr("Enable Motion Controls:\\nWhen enabled it will use the controller's motion control if supported.");
        const QString input_background_controller = tr("Enable Controller Background Input:\\nAllow shadPS4 to detect controller inputs when the game window is not in focus.");
        const QString input_usb_device = tr("USB Device:\\nReal USB Device: Use a real USB Device attached to the system.\\nSkylander Portal: Emulate a Skylander Portal of Power.\\nInfinity Base: Emulate a Disney Infinity Base.\\nDimensions Toypad: Emulate a Lego Dimensions Toypad.");
        const QString input_cursor_section = tr("Cursor:\\nSettings that control when and how the mouse cursor is hidden.");
        const QString input_controller_section = tr("Controller:\\nSettings related to gamepad and mouse input behavior.");
        const QString input_mice_as_mice = tr("Use Mice as Mice:\\nLets the mouse behave as a regular mouse instead of being treated as an emulated controller input.");
        const QString input_circle_confirm = tr("Use Circle Button to Confirm:\\nSwaps the Circle and Cross buttons' roles so Circle acts as the confirm/enter button, matching Japanese console conventions.");
        const QString input_ime_section = tr("IME:\\nSettings for the on-screen keyboard/input method editor used by games for text entry.");
        const QString input_ime_accessibility = tr("Enable IME Accessibility:\\nEnables accessibility features for the on-screen keyboard, such as additional audio/visual cues.");
        const QString input_ime_url_mail_panel = tr("Enable IME URL/Email Short Panel:\\nShows a shortened on-screen keyboard layout optimized for entering URLs and email addresses.");
        const QString input_camera_device = tr("Camera Device:\\nSelects which connected camera device the emulator exposes to games as the PlayStation Camera.");
        //debug
        const QString debug_dump_shaders = tr("Enable Shaders Dumping:\\nFor the sake of technical debugging, saves the game's shaders to a folder as they render.");
        const QString debug_dump = tr("Enable Debug Dumping:\\nSaves the import and export symbols and file header information of the currently running PS4 program to a directory.");
        const QString debug_renderdoc = tr("Enable RenderDoc Debugging:\\nIf enabled, the emulator will provide compatibility with Renderdoc to allow capture and analysis of the currently rendered frame.");
        const QString debug_copy_gpu_buffers = tr("Copy GPU Buffers:\\nGets around race conditions involving GPU submits.\\nMay or may not help with PM4 type 0 crashes.");
        const QString debug_collect_shaders = tr("Collect Shaders:\\nYou need this enabled to edit shaders with the debug menu (Ctrl + F10).");
        const QString debug_crash_diagnostics = tr("Crash Diagnostics:\\nCreates a .yaml file with info about the Vulkan state at the time of crashing.\\nUseful for debugging 'Device lost' errors. If you have this enabled, you should enable Host AND Guest Debug Markers.\\nYou need Vulkan Validation Layers enabled and the Vulkan SDK for this to work.");
        const QString debug_host_markers = tr("Host Debug Markers:\\nInserts emulator-side information like markers for specific AMDGPU commands around Vulkan commands, as well as giving resources debug names.\\nIf you have this enabled, you should enable Crash Diagnostics.\\nUseful for programs like RenderDoc.");
        const QString debug_guest_markers = tr("Guest Debug Markers:\\nInserts any debug markers the game itself has added to the command buffer.\\nIf you have this enabled, you should enable Crash Diagnostics.\\nUseful for programs like RenderDoc.");
        const QString debug_vk_validation = tr("Enable Vulkan Validation Layers:\\nEnables a system that validates the state of the Vulkan renderer and logs information about its internal state.\\nThis will reduce performance and likely change the behavior of emulation.\\nYou need the Vulkan SDK for this to work.");
        const QString debug_vk_core_validation = tr("Enable Core Validation:\\nEnables the main API validation functions.\\nThis will reduce performance and likely change the behavior of emulation.\\nYou need the Vulkan SDK for this to work.");
        const QString debug_vk_sync_validation = tr("Enable Sync Validation:\\nEnables a system that validates the timing of Vulkan rendering tasks.\\nThis will reduce performance and likely change the behavior of emulation.\\nYou need the Vulkan SDK for this to work.");
        const QString debug_vk_gpu_validation = tr("Enable GPU-Assisted Validation:\\nInstruments shaders with code that validates if they are behaving correctly.\\nThis will reduce performance and likely change the behavior of emulation.\\nYou need the Vulkan SDK for this to work.");
        const QString debug_section = tr("Debug:\\nTools for diagnosing crashes and inspecting emulator behavior. Intended for troubleshooting and development, not everyday use.");
        const QString debug_advanced_section = tr("Advanced:\\nLower-level debugging options for diagnosing rendering issues and crashes.");
        const QString debug_vk_validation_section = tr("Vulkan Validation:\\nEnables Vulkan's built-in validation layers to catch incorrect API usage. Reduces performance; requires the Vulkan SDK to be installed.");
        //experimental
        const QString experimental_dma = tr("Enable Direct Memory Access:\\nEnables arbitrary memory access from the GPU to CPU memory.");
        const QString experimental_devkit = tr("Enable Devkit Console Mode:\\nAdds support for Devkit console memory size.");
        const QString experimental_neo = tr("Enable PS4 Neo Mode:\\nAdds support for PS4 Pro emulation and memory size. Currently causes instability in a large number of tested games.");
        const QString experimental_network_connected = tr("Set Network Connected to True:\\nForces games to detect an active network connection. Actual online capabilities are not yet supported.");
        const QString experimental_shadnet = tr("shadNet:\\nCompatibility is very limited at the moment.\\nYou can register at https://www.shadps4.net/shadnet/register/.");
        const QString experimental_shader_cache = tr("Enable Shader Cache:\\nStoring compiled shaders to avoid recompilations, reduce stuttering.");
        const QString experimental_shader_cache_archive = tr("Compress the Shader Cache files into a zip file:\\nThe shader cache files are stored within a single zip file instead of multiple separate files.");
        const QString experimental_dmem = tr("Additional DMem Allocation:\\nForces allocation of the specified amount of additional DMem. Crashes or causes issues in some games.");
        const QString experimental_section = tr("Experimental:\\nFeatures that are still in development or considered unstable. Use with caution - these can cause crashes or unexpected behavior.");
        const QString experimental_shadnet_config = tr("ShadNet Server Settings:\\nConfigure the server addresses used to connect to a ShadNet-compatible online service. Only used while ShadNet is enabled.");
        const QString experimental_shadnet_server = tr("Server:\\nThe address of the ShadNet server to connect to.");
        const QString experimental_shadnet_webapi = tr("WebAPI Server:\\nThe address of the ShadNet WebAPI server used for account and session management.");
        const QString experimental_shadnet_signaling = tr("Signaling Info:\\nConnection details used for ShadNet's peer-to-peer signaling/matchmaking.");
        const QString experimental_upnp = tr("Enable UPnP:\\nAutomatically configure port forwarding on your router via UPnP for ShadNet's networked features.");
        // clang-format on
    } settings;
};
