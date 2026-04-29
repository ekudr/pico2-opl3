cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(opl3 C CXX ASM)

pico_sdk_init()

add_executable(opl3
    opl3.c
    dbopl.cpp
    dbopl_wrapper.cpp
    vgm.c
    audio_pwm.c
    usb_transfer.c
)

pico_set_program_name(opl3 "opl3")
pico_set_program_version(opl3 "0.1")

pico_enable_stdio_uart(opl3 0)
pico_enable_stdio_usb(opl3 1)

target_link_libraries(opl3
    pico_stdlib
    pico_multicore
    hardware_pwm
    hardware_dma
    hardware_irq
    hardware_clocks
)

target_include_directories(opl3 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_compile_definitions(opl3 PRIVATE
    PICO_STACK_SIZE=0x1000
    PICO_CORE1_STACK_SIZE=0x1000
)

pico_set_binary_type(opl3 copy_to_ram)

pico_add_extra_outputs(opl3)
