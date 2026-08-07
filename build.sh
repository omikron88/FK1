#!/bin/bash

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
                             -DFK1_FETCH_SDL3=ON \
                             -DFK1_STATIC_BUILD=ON

cmake --build build --parallel
