cmake_minimum_required(VERSION 3.16)
project(ainovel_ws_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)
find_package(Opus REQUIRED)
find_package(CURL REQUIRED)

# WebRTC VAD源文件
set(WEBRTC_VAD_SOURCES
    third_party/webrtc/common_audio/signal_processing/cross_correlation.c
    third_party/webrtc/common_audio/signal_processing/division_operations.c
    third_party/webrtc/common_audio/signal_processing/downsample_fast.c
    third_party/webrtc/common_audio/signal_processing/energy.c
    third_party/webrtc/common_audio/signal_processing/get_scaling_square.c
    third_party/webrtc/common_audio/signal_processing/min_max_operations.c
    third_party/webrtc/common_audio/signal_processing/resample_48khz.c
    third_party/webrtc/common_audio/signal_processing/resample_by_2_internal.c
    third_party/webrtc/common_audio/signal_processing/resample_fractional.c
    third_party/webrtc/common_audio/signal_processing/spl_init.c
    third_party/webrtc/common_audio/signal_processing/spl_inl.c
    third_party/webrtc/common_audio/signal_processing/vector_scaling_operations.c
    third_party/webrtc/common_audio/vad/vad_core.c
    third_party/webrtc/common_audio/vad/vad_filterbank.c
    third_party/webrtc/common_audio/vad/vad_gmm.c
    third_party/webrtc/common_audio/vad/vad_sp.c
    third_party/webrtc/common_audio/vad/webrtc_vad.c
)

# Add websocketpp as header-only library
add_library(websocketpp INTERFACE)
target_include_directories(websocketpp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/websocketpp)

# Add the executable
add_executable(ws_server 
    src/main.cpp
    src/websocket_server.cpp
    src/binary_protocol.cpp
    ${WEBRTC_VAD_SOURCES}
)

# Include directories
target_include_directories(ws_server PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/webrtc
)

# Platform specific settings
if(WIN32)
    target_link_libraries(ws_server PRIVATE 
        websocketpp
        OpenSSL::SSL 
        OpenSSL::Crypto
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        Opus::opus
        CURL::libcurl
        ws2_32
        wsock32
    )
else()
    target_link_libraries(ws_server PRIVATE 
        websocketpp
        OpenSSL::SSL 
        OpenSSL::Crypto
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        opus
        CURL::libcurl
        pthread
    )
endif()
