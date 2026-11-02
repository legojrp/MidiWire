cmake_minimum_required(VERSION 3.20)
project(MidiWire VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configure libdatachannel build options
set(NO_MEDIA ON CACHE BOOL "" FORCE)
set(NO_WEBSOCKET OFF CACHE BOOL "" FORCE)

# Use local libdatachannel copy in deps/
add_subdirectory(deps/libdatachannel)

# Find system packages
find_package(nlohmann_json REQUIRED)
find_package(OpenSSL REQUIRED)

# Core source files shared across all platforms
set(SOURCES
    src/main.cpp
    src/core/Serializer.cpp
    src/net/JitterBuffer.cpp
    src/net/ClockSynchronizer.cpp
    src/net/NetworkEngine.cpp
    src/net/SignalingClient.cpp
    src/midi/MidiEngine.cpp
    src/app/AppController.cpp
)

# Platform-specific MIDI drivers and packages
if(APPLE)
    list(APPEND SOURCES src/midi/MacMidiDriver.cpp)
elseif(UNIX AND NOT APPLE)
    list(APPEND SOURCES src/midi/AlsaMidiDriver.cpp)
    find_package(ALSA REQUIRED)
elseif(WIN32)
    list(APPEND SOURCES src/midi/WinMidiDriver.cpp)
endif()

# Executable target
add_executable(MidiRemoteApp ${SOURCES})

target_include_directories(MidiRemoteApp PRIVATE include)

# Link common dependencies
target_link_libraries(MidiRemoteApp PRIVATE
    datachannel-static
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Link platform-specific frameworks and native libraries
if(APPLE)
    target_link_libraries(MidiRemoteApp PRIVATE
        "-framework CoreMIDI"
        "-framework CoreFoundation"
    )
elseif(UNIX AND NOT APPLE)
    target_link_libraries(MidiRemoteApp PRIVATE ALSA::ALSA)
elseif(WIN32)
    target_link_libraries(MidiRemoteApp PRIVATE winmm)
endif()