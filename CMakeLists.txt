cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(T4-Display)

spiffs_create_partition_image(storage data FLASH_IN_PROJECT) 