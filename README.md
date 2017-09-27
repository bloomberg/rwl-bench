The file 'bench11.cpp' contains a C++11 implementation of a novel reader-writer lock and supporting code for benchmarking this implementation.  This reader-writer lock was developed in response to the common observation that, even in high-read workloads, a simple mutex can easily outperform reader-writer locks.  This reader-writer lock is designed to allow easy reacquisition of the lock by a thread (a thread can release the lock and immediately reacquire the lock, regardless of the type of lock -- exclusive or shared -- the thread held or requests) where the underlying mutex allows reacquisition.

To build the file:
g++ --std=c++11 bench11.cpp -o bench11 -m64 -O3 -lpthread -lrt

On Darwin, the "-lrt" should be omitted.
