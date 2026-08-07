Doporučená instalace

Nainstaluj MSYS2 z oficiálního webu a spusť položku MSYS2 UCRT64.

Nejprve aktualizuj systém:

pacman -Syu

Pokud se terminál zavře, znovu spusť MSYS2 UCRT64 a aktualizaci dokonči:

pacman -Syu

Potom nainstaluj nástroje:

pacman -S --needed \
    git \
    mingw-w64-ucrt-x86_64-toolchain \
    mingw-w64-ucrt-x86_64-cmake \
    mingw-w64-ucrt-x86_64-ninja \
    mingw-w64-ucrt-x86_64-sdl3

Vlastní sestavení už jde stejně jako v Linuxu:
opět v terminálu MSYS2 UCRT64

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DFK1_FETCH_SDL3=OFF -DFK1_STATIC_BUILD=ON
cmake --build build --parallel

Teoreticky by mohlo fungovat i v MS Visual Studiu, pokud se doinstalují pluginy pro ninja a cmake. Nezkoušel jsem.
