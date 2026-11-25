cmake_minimum_required(VERSION 3.16)

# Общие IDF-компоненты берем из проекта мастера (EXTRA_COMPONENT_DIRS).
set(EXTRA_COMPONENT_DIRS ../master_s3_idf/components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(wireless-audio-satellite-left-idf)

# Custom partition table (8 МБ флеш для S3 сателлита).
set(PARTITION_TABLE partitions.csv)
