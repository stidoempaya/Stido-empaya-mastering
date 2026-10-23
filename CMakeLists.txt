cmake_minimum_required(VERSION 3.22)

# --- Compatibilite macOS -----------------------------------------------------
# A definir AVANT project(). Sans ca, le plugin n'est compatible qu'avec la
# version de macOS de la machine qui compile.
# 10.13 (High Sierra) est la version la plus ancienne acceptee par Xcode recent.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Version minimale de macOS")
# Binaire universel : Mac Intel (x86_64) + Mac Apple Silicon M1/M2/M3/M4 (arm64)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Architectures macOS")

project(StidoEmpayaMastering VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(PLUGIN_FORMATS VST3 AU Standalone)
else()
    set(PLUGIN_FORMATS VST3 Standalone)
endif()

# --- Pro Tools (AAX) ---------------------------------------------------------
# Telecharge le SDK AAX sur developer.avid.com, puis configure avec :
#   cmake -B build -DAAX_SDK_PATH="C:/chemin/vers/AAX_SDK"
set(AAX_SDK_PATH "" CACHE PATH "Chemin vers le SDK AAX d'Avid")
if(AAX_SDK_PATH AND (APPLE OR WIN32))
    juce_set_aax_sdk_path("${AAX_SDK_PATH}")
    list(APPEND PLUGIN_FORMATS AAX)
    message(STATUS "Format AAX active (SDK : ${AAX_SDK_PATH})")
else()
    message(STATUS "Format AAX desactive : definis AAX_SDK_PATH pour Pro Tools")
endif()

option(COPY_AFTER_BUILD "Copier le plugin dans le dossier systeme apres compilation" ON)

juce_add_plugin(StidoEmpayaMastering
    COMPANY_NAME "STIDO"
    PLUGIN_MANUFACTURER_CODE Stdo
    PLUGIN_CODE Emap
    FORMATS ${PLUGIN_FORMATS}
    PRODUCT_NAME "STIDO Empaya Mastering"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD ${COPY_AFTER_BUILD})

target_sources(StidoEmpayaMastering PRIVATE
    source/PluginProcessor.cpp
    source/PluginEditor.cpp)

target_compile_definitions(StidoEmpayaMastering PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_compile_options(StidoEmpayaMastering PUBLIC
    "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

target_link_libraries(StidoEmpayaMastering
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
