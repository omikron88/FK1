#!/bin/bash

sudo apt update

sudo apt install -y \
    build-essential \
    git \
    cmake \
    ninja-build \
    pkg-config \
    zenity \
    libasound2-dev \
    libx11-dev \
    libxext-dev \
    libxrandr-dev \
    libxcursor-dev \
    libxfixes-dev \
    libxi-dev \
    libxss-dev \
    libxtst-dev \
    libxkbcommon-dev \
    libgl1-mesa-dev \
    libegl1-mesa-dev

#Nasledujici jsou volitelne, pro podporu Waylandu a PipeWire

sudo apt install -y \
    libwayland-dev \
    libdecor-0-dev \
    libpipewire-0.3-dev
