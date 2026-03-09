# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/spark-advantage/pokefirered-native/third_party/mgba"
  "/home/spark-advantage/pokefirered-native/build-debug/third_party_build/mgba"
  "/home/spark-advantage/pokefirered-native/build-debug/pfr_mgba_external-prefix"
  "/home/spark-advantage/pokefirered-native/build-debug/pfr_mgba_external-prefix/tmp"
  "/home/spark-advantage/pokefirered-native/build-debug/pfr_mgba_external-prefix/src/pfr_mgba_external-stamp"
  "/home/spark-advantage/pokefirered-native/build-debug/pfr_mgba_external-prefix/src"
  "/home/spark-advantage/pokefirered-native/build-debug/pfr_mgba_external-prefix/src/pfr_mgba_external-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/spark-advantage/pokefirered-native/build-debug/pfr_mgba_external-prefix/src/pfr_mgba_external-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/spark-advantage/pokefirered-native/build-debug/pfr_mgba_external-prefix/src/pfr_mgba_external-stamp${cfgdir}") # cfgdir has leading slash
endif()
