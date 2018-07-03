The file 'bench11.cpp' contains a C++11 implementation of a novel reader-writer lock and supporting code for benchmarking this implementation.  This reader-writer lock was developed in response to the common observation that, even in high-read workloads, a simple mutex can easily outperform reader-writer locks.  This reader-writer lock is designed to allow easy reacquisition of the lock by a thread (a thread can release the lock and immediately reacquire the lock, regardless of the type of lock -- exclusive or shared -- the thread held or requests) where the underlying mutex allows reacquisition.

To build the file:
g++ --std=c++11 bench11.cpp -o bench11 -m64 -O3 -lpthread -lrt

On Darwin, the "-lrt" should be omitted.

The program requires an argument that indicates the percentage, from 0 to 100, of work that should be done with a shared lock (the residual is done with an exclusive lock).

There are also two optional arguments.  The first is the duration for one measurement in milliseconds (20 milliseconds is the default).  The second optional argument is the number of measurements to make (100 by default).  The reported values are the amount of work done per millisecond at the 80th percentile.

Sample output is presented in the 'data' directory.  The filename specifies the platform, that the generating code is this C++11 version of the benchmark, and then the percentage of read work.  The default values for duration and iterations were used in these files.
