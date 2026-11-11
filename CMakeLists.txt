cmake_minimum_required(VERSION 3.22)

project(SF000 VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# JUCE lives in ./JUCE (build.sh clones it on first run, pinned to the tag the suite uses).
add_subdirectory(JUCE)

# One app. It compiles the suite's sound code (./suite, copied from SF Live Suite) and SF-000's own
# engine, sequencer, snapshots and screens (./Source). The offline checks live in the app itself:
#   SF-000 --test            run the audio and timing checks, print a report
#   SF-000 --shot out.png    draw the main screen to a PNG
juce_add_gui_app(SF000
    PRODUCT_NAME                "SF-000"
    COMPANY_NAME                "Strange Frequencies"
    COMPANY_WEBSITE             "https://strangefrequencies.fm"
    BUNDLE_ID                   "fm.strangefrequencies.sf000"
    MICROPHONE_PERMISSION_ENABLED FALSE)

juce_add_binary_data(SF000Assets SOURCES
    assets/fonts/BarlowCondensed-Medium.ttf
    assets/fonts/BarlowCondensed-SemiBold.ttf
    assets/fonts/BarlowSemiCondensed-Medium.ttf
    assets/fonts/BarlowSemiCondensed-SemiBold.ttf
    controllers/e16/rings.lua)

file(GLOB SF_SUITE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/suite/core/*.cpp ${CMAKE_SOURCE_DIR}/suite/modules/*.cpp)
file(GLOB SF_APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Source/*.cpp)

target_sources(SF000 PRIVATE ${SF_SUITE_SOURCES} ${SF_APP_SOURCES})
target_include_directories(SF000 PRIVATE ${CMAKE_SOURCE_DIR}/suite ${CMAKE_SOURCE_DIR}/suite/third_party ${CMAKE_SOURCE_DIR}/Source)
target_compile_definitions(SF000 PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_JACK=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SF000,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SF000,JUCE_VERSION>"
    SF_MODULE_ID="SF000")
target_link_libraries(SF000
    PRIVATE
        SF000Assets
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags)
