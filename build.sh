mkdir -p build
clang++ -I/usr/local/include/SDL2 -L/usr/local/lib -lSDL2 code/main.cpp -o build/satellites
