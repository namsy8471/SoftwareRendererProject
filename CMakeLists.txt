# Headless build: renderer core + srcli + RendererTests (Win32 앱 제외).
# Win32 앱(Win32App)과 GDI 테스트는 SoftrendererProject.sln(MSBuild)로 빌드한다.
#   cmake --preset release && cmake --build --preset release && ctest --preset release
cmake_minimum_required(VERSION 3.25)
project(SoftwareRenderer LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SR_WERROR "Treat first-party warnings as errors" ON)
option(SR_NATIVE "Compile for the build machine's CPU (-march=native); not for shared binaries" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- oneTBB: Windows는 저장소에 포함된 바이너리, 그 외는 시스템 패키지(libtbb-dev 등) ---
if(WIN32)
  set(SR_TBB_ROOT "${CMAKE_SOURCE_DIR}/src/ThirdParty/OneTBB_2023.1.0")
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(SR_TBB_ARCH intel64)
  else()
    set(SR_TBB_ARCH ia32)
  endif()
  add_library(sr_tbb INTERFACE)
  target_include_directories(sr_tbb SYSTEM INTERFACE "${SR_TBB_ROOT}/include")
  target_link_directories(sr_tbb INTERFACE "${SR_TBB_ROOT}/lib/${SR_TBB_ARCH}/vc14")
  set(SR_TBB_DLL_DIR "${SR_TBB_ROOT}/redist/${SR_TBB_ARCH}/vc14")
  set(SR_TBB_TARGET sr_tbb)
else()
  find_package(TBB REQUIRED)
  set(SR_TBB_TARGET TBB::tbb)
endif()

# --- 공통 컴파일 설정 ---
add_library(sr_options INTERFACE)
target_compile_definitions(sr_options INTERFACE SR_NO_MODULES SR_HEADLESS_ONLY NOMINMAX
                                                SR_SOURCE_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
target_include_directories(sr_options INTERFACE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}")
target_include_directories(sr_options SYSTEM INTERFACE "${CMAKE_SOURCE_DIR}/src/ThirdParty")
if(MSVC)
  target_compile_options(sr_options INTERFACE /W4 /utf-8 /permissive- /Zc:__cplusplus /external:W0
                                              /std:c++latest)
  if(SR_WERROR)
    target_compile_options(sr_options INTERFACE /WX)
  endif()
else()
  # MSVC x64 빌드와 같은 SSE4.1 기준선. FP contraction은 꺼서 MSVC(/fp:precise)와 결과를 맞춘다.
  target_compile_options(sr_options INTERFACE -msse4.1 -ffp-contract=off -Wall -Wextra
                                              -Wno-ignored-attributes -Wno-deprecated-copy)
  if(SR_NATIVE)
    target_compile_options(sr_options INTERFACE -march=native)
  endif()
  if(SR_WERROR)
    target_compile_options(sr_options INTERFACE -Werror)
  endif()
endif()

# --- 런타임 검사(CI의 sanitize 프리셋). 예: -DSR_SANITIZE=address,undefined ---
set(SR_SANITIZE "" CACHE STRING "Comma-separated sanitizers (GCC/Clang: address,undefined; MSVC: address)")
if(SR_SANITIZE)
  if(MSVC)
    target_compile_options(sr_options INTERFACE /fsanitize=${SR_SANITIZE})
  else()
    # UB를 경고만 하고 넘어가지 않도록 첫 위반에서 프로세스를 끝낸다.
    target_compile_options(sr_options INTERFACE -fsanitize=${SR_SANITIZE} -fno-sanitize-recover=all
                                                -fno-omit-frame-pointer)
    target_link_options(sr_options INTERFACE -fsanitize=${SR_SANITIZE})
  endif()
endif()

# --- 렌더러 코어: 창/GDI(src/App, src/Platform)를 뺀 전부 ---
file(GLOB_RECURSE SR_CORE_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/src/Core/*.cpp" "${CMAKE_SOURCE_DIR}/src/Graphics/*.cpp"
     "${CMAKE_SOURCE_DIR}/src/Math/*.cpp" "${CMAKE_SOURCE_DIR}/src/Renderer/*.cpp"
     "${CMAKE_SOURCE_DIR}/src/Scene/*.cpp" "${CMAKE_SOURCE_DIR}/src/Utils/*.cpp")
add_library(sr_core STATIC ${SR_CORE_SOURCES})
target_link_libraries(sr_core PUBLIC sr_options ${SR_TBB_TARGET})
# AVX 구현만 AVX로 컴파일한다(런타임 CPUID로 선택). 다른 파일에 AVX가 섞이지 않게 한다.
if(MSVC)
  set_source_files_properties("${CMAKE_SOURCE_DIR}/src/Math/SIMD_AVX.cpp" PROPERTIES COMPILE_OPTIONS "/arch:AVX")
else()
  set_source_files_properties("${CMAKE_SOURCE_DIR}/src/Math/SIMD_AVX.cpp" PROPERTIES COMPILE_OPTIONS "-mavx")
endif()

# --- srcli: headless 벤치마크·PNG 덤프 ---
add_executable(srcli tools/srcli/SrCli.cpp)
target_link_libraries(srcli PRIVATE sr_core)

# --- RendererTests: 같은 테스트 소스, Win32 전용 케이스는 SR_HEADLESS_ONLY로 빠진다 ---
file(GLOB SR_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tests/*.cpp")
add_executable(RendererTests ${SR_TEST_SOURCES})
target_link_libraries(RendererTests PRIVATE sr_core)

if(WIN32)
  foreach(target srcli RendererTests)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${SR_TBB_DLL_DIR}/$<IF:$<CONFIG:Debug>,tbb12_debug.dll,tbb12.dll>" "$<TARGET_FILE_DIR:${target}>")
  endforeach()
  target_link_libraries(sr_tbb INTERFACE $<IF:$<CONFIG:Debug>,tbb12_debug,tbb12>)
endif()

enable_testing()
add_test(NAME RendererTests COMMAND RendererTests "${CMAKE_SOURCE_DIR}")
