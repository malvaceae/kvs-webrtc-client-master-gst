cmake_minimum_required(VERSION 3.14)

project(
  kvsWebrtcClientMasterGst
  VERSION 0.1.0
  LANGUAGES CXX
)

include(FetchContent)
FetchContent_Declare(
  webrtc
  GIT_REPOSITORY https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c.git
  GIT_TAG        v1.12.0
)

set(BUILD_SAMPLE OFF)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
FetchContent_MakeAvailable(webrtc)

include_directories(${webrtc_SOURCE_DIR}/src/include)
include_directories(${webrtc_SOURCE_DIR}/open-source/include)
include_directories(${OPEN_SRC_INSTALL_PREFIX}/include)
link_directories(${webrtc_SOURCE_DIR}/open-source/lib)
link_directories(${OPEN_SRC_INSTALL_PREFIX}/lib)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB2 REQUIRED glib-2.0)
pkg_check_modules(GST REQUIRED gstreamer-1.0)
pkg_check_modules(GST_APP REQUIRED gstreamer-app-1.0)
pkg_check_modules(GOBJ2 REQUIRED gobject-2.0)

include_directories(${GLIB2_INCLUDE_DIRS})
include_directories(${GST_INCLUDE_DIRS})
include_directories(${GST_APP_INCLUDE_DIRS})
include_directories(${GOBJ2_INCLUDE_DIRS})

link_directories(${GLIB2_LIBRARY_DIRS})
link_directories(${GST_LIBRARY_DIRS})
link_directories(${GST_APP_LIBRARY_DIRS})
link_directories(${GOBJ2_LIBRARY_DIRS})

add_executable(
  kvsWebrtcClientMasterGst
  kvsWebrtcClientMasterGst.cpp
  kvs.cpp
)

target_compile_features(
  kvsWebrtcClientMasterGst
  PUBLIC cxx_std_20
)

target_link_libraries(
  kvsWebrtcClientMasterGst
  kvsWebrtcClient
  kvsWebrtcSignalingClient
  kvsCommonLws
  kvspicUtils
  websockets
  ${GLIB2_LIBRARIES}
  ${GST_LIBRARIES}
  ${GST_APP_LIBRARIES}
  ${GOBJ2_LIBRARIES}
)
