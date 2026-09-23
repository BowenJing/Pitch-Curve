cmake_minimum_required(VERSION 3.22)

project(Contour
    VERSION 0.2.0
    DESCRIPTION "Learn and transfer expressive pitch contours"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 3af3ce009f6a02f6fa651008fffb5b41743a9fab
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

FetchContent_Declare(
    signalsmith_stretch
    GIT_REPOSITORY https://git.signalsmith-audio.co.uk/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG 90d6c686eb55b1512575d6d8b581b1959b1e91ac)
FetchContent_MakeAvailable(signalsmith_stretch)

set(CONTOUR_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND CONTOUR_FORMATS AU)
endif()

if(APPLE)
    install(DIRECTORY
        "${CMAKE_CURRENT_BINARY_DIR}/Contour_artefacts/$<CONFIG>/VST3/Contour.vst3"
        DESTINATION "Library/Audio/Plug-Ins/VST3")
    install(DIRECTORY
        "${CMAKE_CURRENT_BINARY_DIR}/Contour_artefacts/$<CONFIG>/AU/Contour.component"
        DESTINATION "Library/Audio/Plug-Ins/Components")
    install(DIRECTORY
        "${CMAKE_CURRENT_BINARY_DIR}/Contour_artefacts/$<CONFIG>/Standalone/Contour.app"
        DESTINATION "Applications")
elseif(WIN32)
    install(DIRECTORY
        "${CMAKE_CURRENT_BINARY_DIR}/Contour_artefacts/$<CONFIG>/VST3/Contour.vst3"
        DESTINATION "VST3")
    install(TARGETS Contour_Standalone RUNTIME DESTINATION "Standalone")
else()
    install(DIRECTORY
        "${CMAKE_CURRENT_BINARY_DIR}/Contour_artefacts/$<CONFIG>/VST3/Contour.vst3"
        DESTINATION "lib/vst3")
    install(PROGRAMS "$<TARGET_FILE:Contour_Standalone>"
        DESTINATION "bin" RENAME "contour")
endif()

juce_add_plugin(Contour
    COMPANY_NAME "Contour Audio"
    BUNDLE_ID "audio.contour.plugin"
    PLUGIN_MANUFACTURER_CODE Cntr
    PLUGIN_CODE Ct01
    PRODUCT_NAME "Contour"
    FORMATS ${CONTOUR_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES "Fx" "Pitch Shift"
    AU_MAIN_TYPE "kAudioUnitType_Effect")

juce_generate_juce_header(Contour)

target_sources(Contour PRIVATE
    Source/PitchDetector.cpp
    Source/PitchDetector.h
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h)

target_include_directories(Contour PRIVATE
    "${signalsmith_stretch_SOURCE_DIR}")

target_compile_definitions(Contour PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Contour PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

if(APPLE)
    set_target_properties(Contour PROPERTIES
        XCODE_ATTRIBUTE_ARCHS "arm64 x86_64"
        OSX_ARCHITECTURES "arm64;x86_64")
endif()

include(CTest)
if(BUILD_TESTING)
    juce_add_console_app(ContourTests PRODUCT_NAME "Contour DSP Tests")
    juce_generate_juce_header(ContourTests)
    target_sources(ContourTests PRIVATE
        Tests/PitchDetectorTests.cpp
        Source/PitchDetector.cpp
        Source/PitchDetector.h)
    target_include_directories(ContourTests PRIVATE Source)
    target_compile_definitions(ContourTests PRIVATE
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0)
    target_link_libraries(ContourTests PRIVATE
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME ContourPitchDetector COMMAND ContourTests)
endif()

set(CPACK_PACKAGE_NAME "Contour")
set(CPACK_PACKAGE_VENDOR "Contour Audio")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Expressive pitch-contour transfer plug-in")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_CHECKSUM "SHA256")
set(CPACK_STRIP_FILES TRUE)

if(APPLE)
    set(CPACK_GENERATOR "productbuild")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/")
    set(CPACK_PRODUCTBUILD_IDENTIFIER "audio.contour.installer")
elseif(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "Contour ${PROJECT_VERSION}")
    set(CPACK_NSIS_PACKAGE_NAME "Contour")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
        "nsExec::ExecToLog 'xcopy /E /I /Y \"$INSTDIR\\VST3\\Contour.vst3\" \"$COMMONFILES64\\VST3\\Contour.vst3\"'")
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
        "RMDir /r \"$COMMONFILES64\\VST3\\Contour.vst3\"")
else()
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Contour Audio")
    set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()

include(CPack)
