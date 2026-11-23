cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(wireless-audio-master-idf)

# Custom partition table для OTA (требует ota_0, ota_1 разделы)
set(PARTITION_TABLE partitions.csv)
