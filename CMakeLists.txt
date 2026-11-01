cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME Explorer_STM32F407)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# FSMC/SRAM 驱动：CubeMX 中没有勾选 FSMC，这里手工加入 HAL 的 SRAM + FSMC 底层驱动。
# 注意：Core/Inc/stm32f4xx_hal_conf.h 中需保持 HAL_SRAM_MODULE_ENABLED 已使能。
# 放在本文件(仅生成一次)而不是 stm32cubemx/CMakeLists.txt(会被重新生成)中，避免丢失。
target_sources(STM32_Drivers PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_sram.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_fsmc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dcmi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_sd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_sdmmc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_spi.c
)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Src/bsp_delay.c
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Src/key.c
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Src/lcd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Src/sccb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Src/ov2640.c
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Src/dcmi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Src/spi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Src/w25qxx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Src/sdio_sdcard.c
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Src/storage.c
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Src/camera.c
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Src/jpeg_view.c
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Src/tjpgd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Src/filemgr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/FatFS/src/ff.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/FatFS/src/diskio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/FatFS/src/ccsbcs.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    ${CMAKE_CURRENT_SOURCE_DIR}/BSP/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/FatFS/src
)

# FatFs 是第三方库，屏蔽其风格类告警
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/FatFS/src/ff.c
    PROPERTIES COMPILE_OPTIONS "-Wno-misleading-indentation"
)

# Add project symbols (macros)
# 自动把工程目录名作为调试项目名称传给源码（main.c 里用作主菜单标题）
get_filename_component(PROJECT_DIR_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    PROJECT_TITLE="${PROJECT_DIR_NAME}"
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
