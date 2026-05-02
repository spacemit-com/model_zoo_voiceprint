cmake_minimum_required(VERSION 3.14)
project(voiceprint C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

# 默认编译类型为 Release，以获得最佳性能
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# 查找依赖库
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# PortAudio：Linux 下从源码构建 ALSA-only 静态库，避免系统 PortAudio 的
# JACK/PulseAudio backend 影响 register_speaker 运行。仓库地址和 tag 可在
# 构建时用 -DPORTAUDIO_GIT_REPOSITORY=... / -DPORTAUDIO_GIT_TAG=... 覆盖。
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  include(FetchContent)
  find_package(ALSA REQUIRED)

  set(PORTAUDIO_GIT_REPOSITORY
    "https://gitee.com/spacemit-robotics/portaudio.git"
    CACHE STRING "PortAudio git repository"
  )
  set(PORTAUDIO_GIT_TAG
    "v19.7.0"
    CACHE STRING "PortAudio git tag or commit"
  )

  set(PA_USE_ALSA ON CACHE BOOL "" FORCE)
  set(PA_USE_JACK OFF CACHE BOOL "" FORCE)
  set(PA_USE_PULSEAUDIO OFF CACHE BOOL "" FORCE)
  set(PA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(PA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(PA_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(PA_BUILD_STATIC ON CACHE BOOL "" FORCE)

  FetchContent_Declare(portaudio
    GIT_REPOSITORY ${PORTAUDIO_GIT_REPOSITORY}
    GIT_TAG ${PORTAUDIO_GIT_TAG}
  )
  FetchContent_MakeAvailable(portaudio)

  set(PORTAUDIO_TARGET portaudio_static)
  set(PORTAUDIO_INCLUDE_DIR ${portaudio_SOURCE_DIR}/include)

  if(TARGET portaudio_static)
    target_compile_options(portaudio_static PRIVATE -w)
  endif()
else()
  # 非 Linux 独立构建保留系统 PortAudio 查找逻辑（Homebrew 默认不提供 CMake 包）
  if(NOT PORTAUDIO_ROOT)
    if(APPLE)
      set(PORTAUDIO_ROOT "/opt/homebrew")
    elseif(WIN32)
      set(PORTAUDIO_ROOT "C:/Program Files/PortAudio")
    else()
      set(PORTAUDIO_ROOT "/usr/local")
    endif()
  endif()

  find_path(PORTAUDIO_INCLUDE_DIR
    NAMES portaudio.h
    PATHS
      ${PORTAUDIO_ROOT}/include
      /usr/local/include
      /opt/homebrew/include
      /usr/include
  )

  find_library(PORTAUDIO_LIBRARY
    NAMES portaudio
    PATHS
      ${PORTAUDIO_ROOT}/lib
      /usr/local/lib
      /opt/homebrew/lib
      /usr/lib
  )

  if(NOT PORTAUDIO_INCLUDE_DIR OR NOT PORTAUDIO_LIBRARY)
    message(FATAL_ERROR "PortAudio not found. 请安装 PortAudio 或设置 PORTAUDIO_ROOT/PORTAUDIO_INCLUDE_DIR/PORTAUDIO_LIBRARY.")
  endif()

  set(PORTAUDIO_TARGET ${PORTAUDIO_LIBRARY})
endif()

# 设置二进制文件和库文件的输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 平台相关的优化编译选项
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv.*")
    message(STATUS "Using RISC-V optimizations")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
      set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=rv64gc -mabi=lp64d -ffast-math -funroll-loops -DNDEBUG")
    else()
      set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=rv32gc -mabi=ilp32d -ffast-math -funroll-loops -DNDEBUG")
    endif()
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
  else()
    # 针对 x86, ARM 等其他架构
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -ffast-math -funroll-loops -flto -DNDEBUG")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto")
  endif()
endif()

# 定义编译选项
option(USE_SPACEMIT_EP "Use the SpaceMIT Execution Provider" OFF)
option(BUILD_TESTS "Build test programs" OFF)

# --- 根据选项决定使用哪个 ONNX Runtime ---
if(USE_SPACEMIT_EP)
  set(SPACEMIT_ORT_ROOT "$ENV{HOME}/spacemit-ort.riscv64.2.0.0+beta2")
  message(STATUS "Using SpaceMIT ONNX Runtime SDK from: ${SPACEMIT_ORT_ROOT}")

  # 使用 NO_DEFAULT_PATH 强制 CMake 只在 SDK 目录里查找，避免系统库干扰
  find_path(ONNXRUNTIME_INCLUDE_DIR NAMES onnxruntime_cxx_api.h PATHS ${SPACEMIT_ORT_ROOT}/include NO_DEFAULT_PATH)
  find_library(ONNXRUNTIME_LIB NAMES onnxruntime PATHS ${SPACEMIT_ORT_ROOT}/lib NO_DEFAULT_PATH)
  find_library(SPACEMIT_EP_LIB NAMES spacemit_ep PATHS ${SPACEMIT_ORT_ROOT}/lib NO_DEFAULT_PATH)

  if(NOT SPACEMIT_EP_LIB OR NOT ONNXRUNTIME_LIB OR NOT ONNXRUNTIME_INCLUDE_DIR)
    message(FATAL_ERROR "Could not find all required files in ${SPACEMIT_ORT_ROOT}. Please check the path.")
  endif()
else()
  # --- 这是不使用SpaceMIT时的默认逻辑 ---
  if(NOT ONNXRUNTIME_ROOT)
    if(APPLE)
      set(ONNXRUNTIME_ROOT "/usr/local")
    elseif(WIN32)
      set(ONNXRUNTIME_ROOT "C:/Program Files/onnxruntime")
    else()
      set(ONNXRUNTIME_ROOT "/usr/local")
    endif()
  endif()

  find_path(ONNXRUNTIME_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h
    PATHS ${ONNXRUNTIME_ROOT}/include
          /opt/homebrew/include
  )

  find_library(ONNXRUNTIME_LIB
    NAMES onnxruntime
    PATHS ${ONNXRUNTIME_ROOT}/lib
          /opt/homebrew/lib
  )

  # 如果在本地找不到，则尝试从网络下载
  if(NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIB)
    message(STATUS "Attempting to download ONNX Runtime...")

    include(FetchContent)
    if(APPLE)
      if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v1.16.3/onnxruntime-osx-arm64-1.16.3.tgz")
      else()
        set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v1.16.3/onnxruntime-osx-x86_64-1.16.3.tgz")
      endif()
    elseif(WIN32)
      set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v1.16.3/onnxruntime-win-x64-1.16.3.zip")
    else()
      set(ONNXRUNTIME_URL "https://github.com/microsoft/onnxruntime/releases/download/v1.16.3/onnxruntime-linux-x64-1.16.3.tgz")
    endif()

    FetchContent_Declare(
      onnxruntime
      URL ${ONNXRUNTIME_URL}
      DOWNLOAD_EXTRACT_TIMESTAMP true
    )
    FetchContent_MakeAvailable(onnxruntime)

    set(ONNXRUNTIME_ROOT ${onnxruntime_SOURCE_DIR})
    find_path(ONNXRUNTIME_INCLUDE_DIR NAMES onnxruntime_cxx_api.h PATHS ${ONNXRUNTIME_ROOT}/include NO_DEFAULT_PATH)
    find_library(ONNXRUNTIME_LIB NAMES onnxruntime PATHS ${ONNXRUNTIME_ROOT}/lib NO_DEFAULT_PATH)
  endif()
endif()

# 最终检查并打印找到的路径
if(NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIB)
  message(FATAL_ERROR "ONNX Runtime not found. Please install or set ONNXRUNTIME_ROOT.")
endif()

message(STATUS "ONNX Runtime include: ${ONNXRUNTIME_INCLUDE_DIR}")
message(STATUS "ONNX Runtime lib: ${ONNXRUNTIME_LIB}")
if(USE_SPACEMIT_EP)
  message(STATUS "SpaceMIT EP lib: ${SPACEMIT_EP_LIB}")
endif()

# 后端选择（分号分隔列表，默认启用 campplus）
set(VP_ENABLED_BACKENDS "campplus" CACHE STRING "Semicolon-separated list of voiceprint backends to build")

# 公共源文件（始终编译）
set(VP_COMMON_SOURCES
  src/wave_reader.cpp
  src/vp_engine.cpp
  src/vp_presets.cpp
  src/vp_backend_factory.cpp
  src/common/mel_filterbank.cpp
)

# 后端源文件（按选项条件编译）
set(VP_BACKEND_SOURCES "")

if("campplus" IN_LIST VP_ENABLED_BACKENDS)
  list(APPEND VP_BACKEND_SOURCES
    src/backends/campplus/campplus_backend.cpp
    src/backends/campplus/onnx_model.cpp
  )
  message(STATUS "voiceprint: enabling CamP+ backend")
endif()

if(NOT VP_BACKEND_SOURCES)
  message(WARNING "voiceprint: no backends enabled, library will have no inference capability")
endif()

set(SOURCES ${VP_COMMON_SOURCES} ${VP_BACKEND_SOURCES})

# 创建核心静态库
add_library(voiceprint STATIC ${SOURCES})

# 为核心库添加包含目录
target_include_directories(voiceprint
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CURL_INCLUDE_DIRS}
  PRIVATE
    ${ONNXRUNTIME_INCLUDE_DIR}
)

# 为核心库链接基础依赖
target_link_libraries(voiceprint
  PUBLIC
    ${ONNXRUNTIME_LIB}
    ${CURL_LIBRARIES}
    Threads::Threads
)
if(UNIX AND NOT APPLE)
  target_link_libraries(voiceprint PUBLIC m)
endif()


# --- 可执行文件定义与链接  ---
# 注意：voiceprint 是静态库，后端通过全局静态对象自注册。
# 必须使用 --whole-archive 强制链接所有 .o，否则链接器会丢弃未引用的后端注册代码。

# 3. 创建可执行文件（从 tools/ 目录）
add_executable(register_speaker
  tools/register_speaker.cpp
  src/audio_recorder.cpp
)
target_include_directories(register_speaker PRIVATE ${PORTAUDIO_INCLUDE_DIR})
target_link_libraries(register_speaker
  PRIVATE
    -Wl,--whole-archive voiceprint -Wl,--no-whole-archive
    ${PORTAUDIO_TARGET}
)

add_executable(identify_speaker tools/identify_speaker.cpp)
target_link_libraries(identify_speaker PRIVATE -Wl,--whole-archive voiceprint -Wl,--no-whole-archive)

# 4. 如果使用了SpaceMIT，补充链接 EP 库并设置 rpath
if(USE_SPACEMIT_EP)
  target_link_libraries(register_speaker PRIVATE ${SPACEMIT_EP_LIB})
  target_link_libraries(identify_speaker PRIVATE ${SPACEMIT_EP_LIB})
  set(CMAKE_INSTALL_RPATH "${SPACEMIT_ORT_ROOT}/lib")
  target_link_options(register_speaker PRIVATE "-Wl,-rpath,${CMAKE_INSTALL_RPATH}")
  target_link_options(identify_speaker PRIVATE "-Wl,-rpath,${CMAKE_INSTALL_RPATH}")
endif()

# --- 安装 ---

install(TARGETS voiceprint register_speaker identify_speaker
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)

install(FILES include/vp_service.h
  DESTINATION include
)
