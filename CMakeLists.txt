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

# macOS draws a window's changed parts as one box round all of them. With the meters in one corner
# and a travelling knob in another that box is most of the screen, drawn 30 times a second. This
# has JUCE draw each changed part on its own (its Metal layer renderer). To compare without it:
#   cmake -B build -DSF000_SPLIT_PAINT=OFF && cmake --build build
option(SF000_SPLIT_PAINT "Draw each changed part of the window on its own (macOS)" ON)

# The suite's instruments and time effects each held their sound back 512 samples so the kick's
# reports reached them in time, whatever order Live ran its plugins in. SF-000 runs the kick's lane
# first, so they need no wait: 0. (512 builds them as the suite does, to compare.)
set(SF000_POCKET_LATENCY 0 CACHE STRING "Samples the suite's pocket holds each module's sound back (the suite's own: 512)")

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
    JUCE_COREGRAPHICS_RENDER_WITH_MULTIPLE_PAINT_CALLS=$<BOOL:${SF000_SPLIT_PAINT}>
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SF000,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SF000,JUCE_VERSION>"
    SF_MODULE_ID="SF000"
    SF_POCKET_LATENCY=${SF000_POCKET_LATENCY})
target_link_libraries(SF000
    PRIVATE
        SF000Assets
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags)
