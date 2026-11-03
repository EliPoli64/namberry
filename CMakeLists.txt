cmake_minimum_required(VERSION 3.22)
project(NAMBerry LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -O2 -Wall -Wextra")

# --- JUCE Integration ---
set(JUCE_DIR "${CMAKE_SOURCE_DIR}/JUCE")

find_package(JUCE REQUIRED CONFIG)

# --- NeuralAmpModelerCore as static library ---
set(NAM_CORE_DIR "${CMAKE_SOURCE_DIR}/deps/NeuralAmpModelerCore")

if(EXISTS "${NAM_CORE_DIR}/CMakeLists.txt")
    add_subdirectory(${NAM_CORE_DIR} NAMCore)
else()
    add_library(NeuralAmpModelerCore STATIC IMPORTED GLOBAL)
    set_property(TARGET NeuralAmpModelerCore PROPERTY IMPORTED_LOCATION
        "${CMAKE_SOURCE_DIR}/deps/NeuralAmpModelerCore/build/libNeuralAmpModelerCore.a")
endif()

# --- Plugin Target ---
add_executable(NAMBerry
    src/nam_model_loader.cpp
    src/main.cpp
    src/ir_loader.cpp
    src/multi_band_eq.cpp
    src/drop_tuner.cpp
    src/digital_capo.cpp
)

target_link_libraries(NAMBerry PRIVATE JUCE::JUCE)

# Link NAM Core statically
target_link_libraries(NAMBerry PRIVATE NeuralAmpModelerCore)

# Eigen alignment flags (per NAM Core README)
target_compile_definitions(NAMBerry PRIVATE EIGEN_MAX_ALIGN_BYTES=0 EIGEN_DONT_VECTORIZE)