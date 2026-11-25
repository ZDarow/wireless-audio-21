# SAME as right but AUDIO_SATELLITE_SIDE=1 is set in right/CMakeLists.txt.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS ../master_s3_idf/components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(wireless-audio-satellite-right-idf)

set(PARTITION_TABLE partitions.csv)
