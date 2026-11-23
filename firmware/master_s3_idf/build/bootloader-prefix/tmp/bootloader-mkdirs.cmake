# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/mi/esp/esp-idf-v5.3.1/components/bootloader/subproject"
  "/media/mi/CC3CD24C3CD230E61/wireless-audio-21/firmware/master_s3_idf/build/bootloader"
  "/media/mi/CC3CD24C3CD230E61/wireless-audio-21/firmware/master_s3_idf/build/bootloader-prefix"
  "/media/mi/CC3CD24C3CD230E61/wireless-audio-21/firmware/master_s3_idf/build/bootloader-prefix/tmp"
  "/media/mi/CC3CD24C3CD230E61/wireless-audio-21/firmware/master_s3_idf/build/bootloader-prefix/src/bootloader-stamp"
  "/media/mi/CC3CD24C3CD230E61/wireless-audio-21/firmware/master_s3_idf/build/bootloader-prefix/src"
  "/media/mi/CC3CD24C3CD230E61/wireless-audio-21/firmware/master_s3_idf/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/media/mi/CC3CD24C3CD230E61/wireless-audio-21/firmware/master_s3_idf/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/media/mi/CC3CD24C3CD230E61/wireless-audio-21/firmware/master_s3_idf/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
