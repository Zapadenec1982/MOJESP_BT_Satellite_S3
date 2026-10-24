# tools/sdio_soak — bare-IDF soak test of the ESP-Hosted SDIO link (docs/SOAK_TEST.md).
# Pulls a TCP/HTTP(S) stream into a PSRAM ring buffer for hours and reports reconnects,
# stalls and the minimum free DMA-SRAM. Same partition table as the player so the NVS
# Wi-Fi credentials the player stored are reused.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(PARTITION_TABLE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/partitions.csv")

project(sdio_soak)
