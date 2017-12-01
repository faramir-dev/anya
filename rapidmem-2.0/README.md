Non-blocking cache of memory chunks
===================================

File `cache.hpp` contains an implementation of a non-blocking cache of memory chunks in namespace `rapidmem`.

Functions:

* rapidmem::cache::alloc() -- Gets one memory chunk from the cache.
* rapidmem::cache::free(T*) -- returns one memory chunk to the cache.
* rapidmem::cache::upkeep() -- adds new chunks to the cache if it is (almost) empty or remove some chunks if it is (almost) full.

Functions `alloc()` and `free()` don't allocate any memory or don't do any blocking operation. On the other hand, `upkeep()` allocates memory with `new[]` and frees it with `delete[]`. It is necessary to call `upkeep()` from time to time, otherwise, other threads may be frozen in `alloc()` or `free()` -- because they may require chunks while the cache is empty or they may try to return chunks while the cache is full.
File `mainc.cpp` contains simple test of the functionality:

    $ g++ -std=gnu++14 -Ofast -o main main.cpp
    $ ./main; echo $?

Compilation for android:

    $ /path/to/sysroot-arm/bin/arm-linux-androideabi-clang++ -static -Ofast -std=gnu++14 -pthread -o main main.cpp
