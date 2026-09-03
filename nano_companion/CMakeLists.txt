# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Separate idf.py project from the ADV app (mini_ft8). ESP-IDF locks target
# and sdkconfig per project, so the two chips cannot share one configure —
# see RFC 0001 §5.1. Pinned here so `idf.py build` works without a manual
# `set-target` step.
set(IDF_TARGET esp32c6)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nano_companion)
