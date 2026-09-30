cmake_minimum_required(VERSION 3.22)

project(Contour
    VERSION 0.7.21
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
    signalsmith-linear
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/linear.git
    GIT_TAG 280e84897520dfec6f039186aba70a5a8cbf2345
    GIT_SHALLOW TRUE)

FetchContent_Declare(
    signalsmith_stretch
    GIT_REPOSITORY https://git.signalsmith-audio.co.uk/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG 90d6c686eb55b1512575d6d8b581b1959b1e91ac)
FetchContent_MakeAvailable(signalsmith_stretch)

set(CONTOUR_FORMATS VST3 Standalone)
set(CONTOUR_VST3_AUTO_MANIFEST TRUE)
if(APPLE)
    list(APPEND CONTOUR_FORMATS AU)
endif()
if(CMAKE_CROSSCOMPILING)
    # JUCE's manifest scanner is a target executable and cannot run on the
    # Linux build host. VST3 hosts support bundles without moduleinfo.json.
    set(CONTOUR_VST3_AUTO_MANIFEST FALSE)
endif()

juce_add_plugin(Contour
    COMPANY_NAME "Circus Audio"
    BUNDLE_ID "audio.circus.pitchcurve"
    PLUGIN_MANUFACTURER_CODE Cntr
    PLUGIN_CODE Ct01
    PRODUCT_NAME "Pitch Curve"
    FORMATS ${CONTOUR_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_AUTO_MANIFEST ${CONTOUR_VST3_AUTO_MANIFEST}
    VST3_CATEGORIES "Fx" "Pitch Shift"
    AU_MAIN_TYPE "kAudioUnitType_Effect")

juce_generate_juce_header(Contour)

if(CMAKE_CROSSCOMPILING AND WIN32)
    find_program(WINE_EXECUTABLE wine REQUIRED)
    _juce_add_vst3_manifest_helper_target(Contour)
    if(NOT TARGET Contour_vst3_helper)
        message(FATAL_ERROR "JUCE did not create the VST3 manifest helper target")
    endif()

    set(CONTOUR_VST3_MANIFEST
        "${CMAKE_CURRENT_BINARY_DIR}/Contour_artefacts/$<CONFIG>/VST3/Pitch Curve.vst3/Contents/Resources/moduleinfo.json")
    add_dependencies(Contour_VST3 Contour_vst3_helper)
    add_custom_command(TARGET Contour_VST3 POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DHELPER=$<TARGET_FILE:Contour_vst3_helper>"
            "-DWINE=${WINE_EXECUTABLE}"
            "-DOUTPUT=${CONTOUR_VST3_MANIFEST}"
            "-DEXPECTED_NAME=Pitch Curve"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateVST3Manifest.cmake"
        BYPRODUCTS "${CONTOUR_VST3_MANIFEST}"
        COMMENT "Generating Windows VST3 moduleinfo.json with Wine"
        VERBATIM)
endif()

target_sources(Contour PRIVATE
    Source/PitchDetector.cpp
    Source/PitchDetector.h
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h)

target_compile_definitions(Contour PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Contour PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    signalsmith-stretch
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

if(APPLE)
    set_target_properties(Contour PROPERTIES
        XCODE_ATTRIBUTE_ARCHS "arm64 x86_64"
        OSX_ARCHITECTURES "arm64;x86_64")
endif()

if(UNIX AND NOT APPLE)
    target_compile_options(Contour PRIVATE -fstack-protector-strong)
    target_compile_definitions(Contour PRIVATE _FORTIFY_SOURCE=3)
    target_link_options(Contour_VST3 PRIVATE -Wl,-z,relro,-z,now)
    target_link_options(Contour_Standalone PRIVATE -Wl,-z,relro,-z,now)
elseif(MINGW)
    target_compile_options(Contour PRIVATE -fstack-protector-strong)
    target_link_options(Contour_VST3 PRIVATE
        -static -Wl,--dynamicbase,--nxcompat,--high-entropy-va)
    target_link_options(Contour_Standalone PRIVATE
        -static -Wl,--dynamicbase,--nxcompat,--high-entropy-va)
endif()

if(APPLE)
    install(DIRECTORY
        "${CMAKE_CURRENT_BINARY_DIR}/Contour_artefacts/$<CONFIG>/VST3/Pitch Curve.vst3"
        DESTINATION "Library/Audio/Plug-Ins/VST3" COMPONENT ContourRuntime)
    install(DIRECTORY
        "${CMAKE_CURRENT_BINARY_DIR}/Contour_artefacts/$<CONFIG>/AU/Pitch Curve.component"
        DESTINATION "Library/Audio/Plug-Ins/Components" COMPONENT ContourRuntime)
    install(DIRECTORY
        "${CMAKE_CURRENT_BINARY_DIR}/Contour_artefacts/$<CONFIG>/Standalone/Pitch Curve.app"
        DESTINATION "Applications" COMPONENT ContourRuntime)
elseif(WIN32)
    install(DIRECTORY
        "${CMAKE_CURRENT_BINARY_DIR}/Contour_artefacts/$<CONFIG>/VST3/Pitch Curve.vst3"
        DESTINATION "VST3" COMPONENT ContourRuntime)
    install(TARGETS Contour_Standalone RUNTIME DESTINATION "Standalone"
        COMPONENT ContourRuntime)
else()
    install(DIRECTORY
        "${CMAKE_CURRENT_BINARY_DIR}/Contour_artefacts/$<CONFIG>/VST3/Pitch Curve.vst3"
        DESTINATION "lib/vst3" COMPONENT ContourRuntime)
    install(PROGRAMS "$<TARGET_FILE:Contour_Standalone>"
        DESTINATION "bin" RENAME "pitchcurve" COMPONENT ContourRuntime)
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

    juce_add_console_app(ContourProcessorTests PRODUCT_NAME "Contour Processor Tests")
    juce_generate_juce_header(ContourProcessorTests)
    target_sources(ContourProcessorTests PRIVATE
        Tests/PluginProcessorTests.cpp
        Source/PitchDetector.cpp
        Source/PitchDetector.h
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h)
    target_include_directories(ContourProcessorTests PRIVATE Source)
    target_compile_definitions(ContourProcessorTests PRIVATE
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JucePlugin_Name="Pitch Curve")
    target_link_libraries(ContourProcessorTests PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
        signalsmith-stretch
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME ContourProcessorSafety COMMAND ContourProcessorTests)
endif()

set(CPACK_PACKAGE_NAME "Pitch Curve")
set(CPACK_PACKAGE_VENDOR "Circus Audio")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Expressive pitch-contour transfer plug-in")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/COPYING")
set(CPACK_PACKAGE_CHECKSUM "SHA256")
set(CPACK_STRIP_FILES TRUE)
set(CPACK_COMPONENTS_ALL ContourRuntime)
set(CPACK_INSTALL_CMAKE_PROJECTS
    "${CMAKE_BINARY_DIR};${PROJECT_NAME};ContourRuntime;/")

if(APPLE)
    set(CPACK_GENERATOR "productbuild")
    set(CPACK_PACKAGE_FILE_NAME "Pitch-Curve-${PROJECT_VERSION}-macOS-Universal")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/")
    set(CPACK_PRODUCTBUILD_IDENTIFIER "audio.circus.pitchcurve.installer")
elseif(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
        set(CONTOUR_WINDOWS_ARCH "arm64")
    else()
        set(CONTOUR_WINDOWS_ARCH "x64")
    endif()
    set(CPACK_PACKAGE_FILE_NAME
        "Pitch-Curve-${PROJECT_VERSION}-Windows-${CONTOUR_WINDOWS_ARCH}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "Circus Audio\\\\Pitch Curve")
    set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "Contour")
    set(CPACK_NSIS_DISPLAY_NAME "Pitch Curve ${PROJECT_VERSION}")
    set(CPACK_NSIS_PACKAGE_NAME "Pitch Curve")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_DEFINES
        [=[!define MUI_STARTMENUPAGE_DEFAULTFOLDER \"Circus Audio\"]=])
    set(CPACK_NSIS_MENU_LINKS "Standalone/Pitch Curve.exe" "Pitch Curve")
    set(CPACK_NSIS_EXTRA_PREINSTALL_COMMANDS [=[
RMDir /r \"$PROGRAMFILES64\\Contour\"
RMDir /r \"$SMPROGRAMS\\Contour\"
RMDir /r \"$SMPROGRAMS\\Pitch Curve\"
DeleteRegKey SHCTX \"Software\\Contour Audio\\Contour\"
RMDir /r \"$PROGRAMFILES64\\Sound Tools\\PitchTransform\"
RMDir /r \"$SMPROGRAMS\\PitchTransform\"
DeleteRegKey SHCTX \"Software\\Sound Tools\\Contour\"
RMDir /r \"$COMMONFILES64\\VST3\\Contour.vst3\"
RMDir /r \"$COMMONFILES64\\VST3\\PitchTransform.vst3\"
]=])
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS [=[
SetOutPath \"$COMMONFILES64\\VST3\\Pitch Curve.vst3\"
ClearErrors
File /r \"\${INST_DIR}\\VST3\\Pitch Curve.vst3\\*.*\"
IfErrors vst3InstallFailed vst3InstallDone
vst3InstallFailed:
  MessageBox MB_ICONSTOP \"Pitch Curve VST3 could not be installed.\"
  Abort
vst3InstallDone:
SetOutPath \"$INSTDIR\"
]=])
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS [=[
RMDir /r \"$COMMONFILES64\\VST3\\Pitch Curve.vst3\"
]=])
else()
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_PACKAGE_FILE_NAME
        "Pitch-Curve-${PROJECT_VERSION}-Linux-${CMAKE_SYSTEM_PROCESSOR}")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Circus Audio")
    set(CPACK_DEBIAN_PACKAGE_NAME "pitch-curve")
    set(CPACK_DEBIAN_PACKAGE_CONFLICTS "pitchtransform, contour")
    set(CPACK_DEBIAN_PACKAGE_REPLACES "pitchtransform, contour")
    set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()

include(CPack)
