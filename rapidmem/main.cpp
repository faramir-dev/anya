#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <thread>

#include "cache.hpp"

namespace {

rapidmem::cache<int> cache{4096, 1024};

typedef int* Chunk;

void test(unsigned seed) {
	for (int i = 0; i < 100; ++i) {
		const int chunks_num = rand_r(&seed) % 20;
		const int sleep_nanos = rand_r(&seed) % 100000;
		const int b = rand_r(&seed) % 256; // Value that will be stored to every chunk

		Chunk chunks[chunks_num];
		std::generate(chunks, chunks + chunks_num, []{ return cache.alloc(); });
		std::for_each(chunks, chunks + chunks_num, [b](Chunk &chunk) { std::fill(chunk, chunk + 4096, b); });
		std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_nanos));
		for (int j = 0; j < chunks_num; ++j) {
			for (int k = 0; k < 4096; ++k) {
				if (chunks[j][k] != b) {
					fprintf(stderr, "Difference: %d/%d/%d\n", i, j, k);
					abort();
				}
			}
		}
		std::for_each(chunks, chunks + chunks_num, [](Chunk &chunk) { cache.free(chunk); });
	}
}

std::atomic<bool> upkeep_run{true};
void upkeep() {
	while(upkeep_run.load()) {
		cache.upkeep();
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

} /* anonymous namespoace */

int
main(void) {
	std::array<std::thread, 12> thes;
	for(auto &the: thes) { the = std::thread{test, rand()}; }
	std::thread the_upkeep{upkeep};
	for(auto &the: thes) { the.join(); }
	upkeep_run.store(false);
	the_upkeep.join();
	return 0;
}
