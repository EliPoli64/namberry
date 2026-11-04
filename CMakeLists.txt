cmake_minimum_required(VERSION 3.22)
project(NAMBerry VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- JUCE Integration ---
set(JUCE_DIR "${CMAKE_SOURCE_DIR}/JUCE")
if(EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    find_package(JUCE REQUIRED CONFIG)
endif()

# --- NAM inference (NeuralAudio, as used by neural-amp-modeler-lv2) ---
set(NA_DIR "${CMAKE_SOURCE_DIR}/deps/NeuralAudio")
if(EXISTS "${NA_DIR}/CMakeLists.txt")
    # Trim optional heavy pieces we do not need.
    set(BUILD_UTILS OFF CACHE BOOL "" FORCE)
    add_subdirectory(${NA_DIR} NeuralAudioBuild)
    set(NAM_LINK_TARGET NeuralAudio)
    set(NAM_AVAILABLE TRUE)
else()
    message(WARNING "NeuralAudio not found at ${NA_DIR}; building without NAM inference.")
    add_library(NAMStub INTERFACE)
    set(NAM_LINK_TARGET NAMStub)
    set(NAM_AVAILABLE FALSE)
endif()

# --- Plugin Target ---
juce_add_plugin(NAMBerry
    COMPANY_NAME "NAMberry"
    PLUGIN_NAME "NAMberry"
    VERSION "0.1.0"
    BUNDLE_ID "com.elipoli.namberry"
    IS_SYNTH FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    PLUGIN_MANUFACTURER_CODE NmBr
    PLUGIN_CODE NmBy
    FORMATS VST3 Standalone
    PRODUCT_NAME "NAMberry"
)

juce_generate_juce_header(NAMBerry)

# --- Source Files ---
target_sources(NAMBerry PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/directory_browser.cpp
    src/nam_model_loader.cpp
    src/ir_loader.cpp
    src/multi_band_eq.cpp
    src/drop_tuner.cpp
    src/digital_capo.cpp
    src/pitch_shifter.cpp
    src/pitch_shift_engines.cpp
    src/modulation_effects.cpp
    src/compressor.cpp
    src/noise_gate.cpp
    src/room.cpp
    src/post_effects.cpp
)

target_include_directories(NAMBerry
    PRIVATE
        src
        include
    PUBLIC
        "${CMAKE_SOURCE_DIR}/deps/signalsmith-stretch"
        "${CMAKE_SOURCE_DIR}/deps/signalsmith-linear/include"
)

# --- Link Libraries ---
target_link_libraries(NAMBerry
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_basics
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

if(NAM_AVAILABLE)
    target_link_libraries(NAMBerry PRIVATE ${NAM_LINK_TARGET})
    target_include_directories(NAMBerry PRIVATE "${NA_DIR}")
endif()

# --- Compile Definitions & Eigen Flags ---
target_compile_definitions(NAMBerry PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    EIGEN_MAX_ALIGN_BYTES=0
    EIGEN_DONT_VECTORIZE
)

if(NAM_AVAILABLE)
    target_compile_definitions(${NAM_LINK_TARGET} PRIVATE
        EIGEN_MAX_ALIGN_BYTES=0
        EIGEN_DONT_VECTORIZE
    )
endif()

# --- Offline render smoke test ---
juce_add_console_app(NAMBerryOfflineTest
    PRODUCT_NAME "NAMBerryOfflineTest"
)

target_sources(NAMBerryOfflineTest PRIVATE tests/offline_render.cpp)
target_include_directories(NAMBerryOfflineTest PRIVATE src include)

target_link_libraries(NAMBerryOfflineTest
    PRIVATE
        NAMBerry
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_basics
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

target_compile_definitions(NAMBerryOfflineTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

if(NAM_AVAILABLE)
    target_link_libraries(NAMBerryOfflineTest PRIVATE ${NAM_LINK_TARGET})
    target_include_directories(NAMBerryOfflineTest PRIVATE "${NA_DIR}")
endif()

# --- UI snapshot tool (renders each tab to PNG headlessly) ---
juce_add_console_app(NAMBerryUiSnapshot
    PRODUCT_NAME "NAMBerryUiSnapshot"
)

target_sources(NAMBerryUiSnapshot PRIVATE tests/ui_snapshot.cpp)
target_include_directories(NAMBerryUiSnapshot PRIVATE src include)

target_link_libraries(NAMBerryUiSnapshot
    PRIVATE
        NAMBerry
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_basics
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

target_compile_definitions(NAMBerryUiSnapshot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

if(NAM_AVAILABLE)
    target_link_libraries(NAMBerryUiSnapshot PRIVATE ${NAM_LINK_TARGET})
    target_include_directories(NAMBerryUiSnapshot PRIVATE "${NA_DIR}")
endif()
