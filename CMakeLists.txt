cmake_minimum_required(VERSION 3.13)
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
include(${PICO_EXTRAS_PATH}/external/pico_extras_import.cmake)

project(furnace C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

option(FLASH "Enable saving user config to flash memory" ON)

set(SOURCES
    furnace.c
    spi.c
    max318xx.c
    logger.c
    )

set(DEFINES
     WIFI_SSID=\"${WIFI_SSID}\"
     WIFI_PASSWORD=\"${WIFI_PASSWORD}\"
     CYW43_HOST_NAME=\"${CONFIG_HOSTNAME}\"
    )

if(FLASH)
  list(APPEND SOURCES flash_io.c)
  list(APPEND DEFINES CONFIG_FLASH=1)
endif()

pico_sdk_init()

add_executable(${PROJECT_NAME}
       ${SOURCES}
       )
target_compile_definitions(furnace PRIVATE
       ${DEFINES}
       )
target_include_directories(furnace PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts
        )
target_link_libraries(furnace
        pico_cyw43_arch_lwip_poll
        pico_stdlib
        )
pico_add_extra_outputs(${PROJECT_NAME})
pico_set_linker_script(${PROJECT_NAME} ${CMAKE_SOURCE_DIR}/memmap.ld)

target_link_libraries(${PROJECT_NAME} pico_stdlib hardware_spi hardware_pwm)


pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)
