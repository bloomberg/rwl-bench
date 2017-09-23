g++ --std=c++11 bench11.cpp -o bench11 -m64 -O3 -lpthread -lrt

On Darwin, the "-lrt" should be omitted.
