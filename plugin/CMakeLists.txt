# Included from the top-level project (JUCE and, optionally, clap-juce-extensions are already available).

# Product name / target in one place.
set(T3X4Z_PLUGIN T3X4Z)
set(T3X4Z_PRODUCT_NAME "T3X4Z")
set(T3X4Z_CLAP_ID "com.jg.t3x4z")      # reverse-DNS; change to a domain you control before release

# Formats per platform: VST3 + Standalone everywhere, AU on macOS. CLAP is added below.
set(T3X4Z_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND T3X4Z_FORMATS AU)
endif()

juce_add_plugin(${T3X4Z_PLUGIN}
    COMPANY_NAME "JG"
    PRODUCT_NAME "${T3X4Z_PRODUCT_NAME}"
    PLUGIN_MANUFACTURER_CODE JGaa
    PLUGIN_CODE Tx81
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    FORMATS ${T3X4Z_FORMATS}
    COPY_PLUGIN_AFTER_BUILD ${T3X4Z_INSTALL_AFTER_BUILD})

# Embedded UI font: Comic Neue Regular (SIL Open Font License 1.1; keep third_party/Comic_Neue/OFL.txt with it).
juce_add_binary_data(T3X4ZAssets SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/Comic_Neue/ComicNeue-Regular.ttf)
set_target_properties(T3X4ZAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_sources(${T3X4Z_PLUGIN} PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)

# No web view / networking: avoids the GTK3, WebKit and libcurl dependencies on Linux entirely.
target_compile_definitions(${T3X4Z_PLUGIN} PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(${T3X4Z_PLUGIN}
    PRIVATE
        tx81z_core
        T3X4ZAssets
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(T3X4Z_BUILD_CLAP)
    clap_juce_extensions_plugin(TARGET ${T3X4Z_PLUGIN}
        CLAP_ID "${T3X4Z_CLAP_ID}"
        CLAP_FEATURES instrument synthesizer stereo)
endif()

# Host harness: drives the processor headless like a DAW would.
if(T3X4Z_BUILD_TESTS)
    juce_add_console_app(ProcessorTest PRODUCT_NAME "ProcessorTest")
    target_sources(ProcessorTest PRIVATE Test/ProcessorTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(ProcessorTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(ProcessorTest PRIVATE tx81z_core T3X4ZAssets
        juce::juce_audio_formats juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics
        juce::juce_recommended_config_flags)
endif()