cmake_minimum_required(VERSION 3.24)

project(FK1Emulator VERSION 0.1.0 LANGUAGES C CXX)

option(FK1_BUILD_FRONTEND "Build the SDL3 desktop frontend" ON)
option(FK1_BUILD_TESTS "Build FK-1 core tests" ON)
option(FK1_FETCH_SDL3 "Download SDL3 when it is not installed" ON)
option(FK1_FETCH_Z80 "Download the pinned Z80 and Zeta sources" ON)
option(FK1_STATIC_BUILD "Statically link bundled runtime libraries" OFF)
set(FK1_SDL3_VERSION "3.4.14" CACHE STRING "SDL3 version used by FetchContent")
set(FK1_IMGUI_VERSION "v1.91.9b" CACHE STRING "Dear ImGui revision used by FetchContent")
set(FK1_Z80_COMMIT "f2c01c639b850373da3b46d4fe71241d852179eb" CACHE STRING "redcode/Z80 revision")
set(FK1_ZETA_COMMIT "93ba5ab967eef00f074d21bb760fe9dc48afd2d3" CACHE STRING "redcode/Zeta revision")
set(FK1_BOOT_ROM "${CMAKE_CURRENT_SOURCE_DIR}/roms/boot.rom" CACHE FILEPATH "ROM image embedded in FK-1 binaries")

if(FK1_STATIC_BUILD
   AND NOT MINGW
   AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "FK1_STATIC_BUILD is supported only with MinGW/MSYS2 or Linux.")
endif()

function(fk1_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    endif()
endfunction()

if(NOT EXISTS "${FK1_BOOT_ROM}")
    message(FATAL_ERROR "FK-1 boot ROM was not found: ${FK1_BOOT_ROM}")
endif()

file(SIZE "${FK1_BOOT_ROM}" fk1_boot_rom_size)
if(NOT fk1_boot_rom_size EQUAL 2048
   AND NOT fk1_boot_rom_size EQUAL 4096
   AND NOT fk1_boot_rom_size EQUAL 8192
   AND NOT fk1_boot_rom_size EQUAL 16384)
    message(FATAL_ERROR "FK-1 boot ROM must contain exactly 2, 4, 8 or 16 KiB.")
endif()

file(READ "${FK1_BOOT_ROM}" fk1_boot_rom_hex HEX)
string(REGEX REPLACE "([0-9a-fA-F][0-9a-fA-F])" "0x\\1, " fk1_boot_rom_bytes "${fk1_boot_rom_hex}")
set(fk1_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${fk1_generated_dir}")
file(CONFIGURE
    OUTPUT "${fk1_generated_dir}/embedded_boot_rom.cpp"
    CONTENT [=[
#include "fk1/embedded_rom.hpp"

#include <array>

namespace {

constexpr std::array<std::uint8_t, @fk1_boot_rom_size@> boot_rom{
    @fk1_boot_rom_bytes@
};

} // namespace

namespace fk1 {

std::span<const std::uint8_t> embedded_boot_rom() noexcept
{
    return std::span<const std::uint8_t>{boot_rom};
}

} // namespace fk1
]=]
    @ONLY
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FK1_BOOT_ROM}")

if(FK1_FETCH_Z80)
    include(FetchContent)

    set(Zeta_WITH_CMAKE_SUPPORT OFF CACHE BOOL "" FORCE)
    set(Zeta_WITH_HTML_DOCUMENTATION OFF CACHE BOOL "" FORCE)
    set(Zeta_WITH_PDF_DOCUMENTATION OFF CACHE BOOL "" FORCE)
    set(Zeta_WITH_PKGCONFIG_SUPPORT OFF CACHE BOOL "" FORCE)
    set(Zeta_WITH_STANDARD_DOCUMENTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        Zeta
        GIT_REPOSITORY https://github.com/redcode/Zeta.git
        GIT_TAG ${FK1_ZETA_COMMIT}
    )
    FetchContent_MakeAvailable(Zeta)

    set(Z80_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(Z80_WITH_EXECUTE OFF CACHE BOOL "" FORCE)
    set(Z80_WITH_Q ON CACHE BOOL "" FORCE)
    set(Z80_WITH_ZILOG_NMOS_LD_A_IR_BUG ON CACHE BOOL "" FORCE)
    set(Z80_WITH_CMAKE_SUPPORT OFF CACHE BOOL "" FORCE)
    set(Z80_WITH_HTML_DOCUMENTATION OFF CACHE BOOL "" FORCE)
    set(Z80_WITH_PDF_DOCUMENTATION OFF CACHE BOOL "" FORCE)
    set(Z80_WITH_PKGCONFIG_SUPPORT OFF CACHE BOOL "" FORCE)
    set(Z80_WITH_STANDARD_DOCUMENTS OFF CACHE BOOL "" FORCE)
    set(Z80_WITH_STEP_TESTING_TOOL OFF CACHE BOOL "" FORCE)
    set(Z80_WITH_TESTING_TOOL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        Z80
        GIT_REPOSITORY https://github.com/redcode/Z80.git
        GIT_TAG ${FK1_Z80_COMMIT}
    )
    FetchContent_MakeAvailable(Z80)
else()
    find_package(Z80 REQUIRED Static)
endif()

add_library(fk1_core STATIC
    src/core/disk_image_8sd.cpp
    src/core/floppy.cpp
    src/core/interrupt_controller3214.cpp
    src/core/keyboard.cpp
    src/core/machine.cpp
    src/core/pit8253.cpp
    src/core/ppi8255.cpp
    src/core/uart8251.cpp
    "${fk1_generated_dir}/embedded_boot_rom.cpp"
)
add_library(FK1::core ALIAS fk1_core)

target_include_directories(fk1_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(fk1_core PUBLIC cxx_std_20)
target_link_libraries(fk1_core PRIVATE Z80)
fk1_enable_warnings(fk1_core)

if(FK1_BUILD_FRONTEND)
    if(FK1_STATIC_BUILD)
        find_package(SDL3 3.2 QUIET CONFIG COMPONENTS SDL3-static)
    else()
        find_package(SDL3 3.2 CONFIG QUIET)
    endif()

    if(NOT TARGET SDL3::SDL3 OR (FK1_STATIC_BUILD AND NOT TARGET SDL3::SDL3-static))
        if(NOT FK1_FETCH_SDL3)
            message(FATAL_ERROR "The required SDL3 target was not found. Install SDL3 or configure with -DFK1_FETCH_SDL3=ON.")
        endif()

        include(FetchContent)
        set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        set(SDL_TESTS OFF CACHE BOOL "" FORCE)
        if(FK1_STATIC_BUILD)
            set(SDL_SHARED OFF CACHE BOOL "" FORCE)
            set(SDL_STATIC ON CACHE BOOL "" FORCE)
        endif()
        FetchContent_Declare(
            SDL3
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG release-${FK1_SDL3_VERSION}
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(SDL3)
    endif()

    if(FK1_STATIC_BUILD)
        set(fk1_sdl_target SDL3::SDL3-static)
    else()
        set(fk1_sdl_target SDL3::SDL3)
    endif()

    include(FetchContent)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG ${FK1_IMGUI_VERSION}
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(imgui)

    add_library(fk1_imgui STATIC
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp"
    )
    target_include_directories(fk1_imgui SYSTEM PUBLIC
        "${imgui_SOURCE_DIR}"
        "${imgui_SOURCE_DIR}/backends"
    )
    target_link_libraries(fk1_imgui PUBLIC ${fk1_sdl_target})

    add_executable(fk1
        src/frontend/disk_files.cpp
        src/frontend/main.cpp
        src/frontend/serial_tcp.cpp
        src/frontend/ui.cpp
    )
    target_link_libraries(fk1 PRIVATE FK1::core fk1_imgui)
    if(WIN32)
        target_link_libraries(fk1 PRIVATE ws2_32)
    endif()
    target_compile_features(fk1 PRIVATE cxx_std_20)
    fk1_enable_warnings(fk1)

    if(FK1_STATIC_BUILD AND MINGW)
        target_link_options(fk1 PRIVATE -static -static-libgcc -static-libstdc++)
    elseif(FK1_STATIC_BUILD AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Keep glibc and the host graphics stack dynamic. SDL3 itself remains
        # static and loads the installed X11/Wayland/graphics drivers at run time.
        target_link_options(fk1 PRIVATE -static-libgcc -static-libstdc++)
    endif()

    if(WIN32)
        set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT fk1)
    endif()
endif()

if(FK1_BUILD_TESTS)
    enable_testing()

    add_executable(fk1_core_tests
        tests/machine_tests.cpp
    )
    target_link_libraries(fk1_core_tests PRIVATE FK1::core)
    target_compile_features(fk1_core_tests PRIVATE cxx_std_20)
    target_compile_definitions(
        fk1_core_tests PRIVATE FK1_BOOT_DISK_IMAGE="${CMAKE_CURRENT_SOURCE_DIR}/disk/boot.8sd")
    fk1_enable_warnings(fk1_core_tests)

    add_test(NAME fk1_core_tests COMMAND fk1_core_tests)

    add_executable(fk1_peripheral_tests
        tests/peripheral_tests.cpp
    )
    target_link_libraries(fk1_peripheral_tests PRIVATE FK1::core)
    target_compile_features(fk1_peripheral_tests PRIVATE cxx_std_20)
    fk1_enable_warnings(fk1_peripheral_tests)

    add_test(NAME fk1_peripheral_tests COMMAND fk1_peripheral_tests)

    add_executable(fk1_serial_tcp_tests
        tests/serial_tcp_tests.cpp
        src/frontend/serial_tcp.cpp
    )
    target_include_directories(fk1_serial_tcp_tests PRIVATE src/frontend)
    target_compile_features(fk1_serial_tcp_tests PRIVATE cxx_std_20)
    if(WIN32)
        target_link_libraries(fk1_serial_tcp_tests PRIVATE ws2_32)
    endif()
    fk1_enable_warnings(fk1_serial_tcp_tests)

    add_test(NAME fk1_serial_tcp_tests COMMAND fk1_serial_tcp_tests)

    add_executable(fk1_disk_file_tests
        tests/disk_file_tests.cpp
        src/frontend/disk_files.cpp
    )
    target_include_directories(fk1_disk_file_tests PRIVATE src/frontend)
    target_link_libraries(fk1_disk_file_tests PRIVATE FK1::core)
    target_compile_features(fk1_disk_file_tests PRIVATE cxx_std_20)
    fk1_enable_warnings(fk1_disk_file_tests)

    add_test(NAME fk1_disk_file_tests COMMAND fk1_disk_file_tests)
endif()
