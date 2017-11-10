#include "cache.hpp"

int
main(void) {
	rapidmem::cache<int> cache{4096, 1024};
	return 0;
}
