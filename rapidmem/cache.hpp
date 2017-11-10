#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <type_traits>

namespace rapidmem {

template <typename T, unsigned M = 4>
class cache {
	static_assert(std::is_pod<T>::value, "Only PODs supported");
	static_assert(M >= 3, "upkeep() not only alocates chunks but also frees them if there is more than (M-1)/M chunks in the queue");

	::size_t chunk_size_;
	::size_t chunks_num_;
	std::atomic<::uint64_t> beg_, end_;
	std::unique_ptr<std::atomic<T*>[]> queue_;

	T* get_chunk() {
		::uint64_t beg, end;
		::uint64_t slot;
		T* chunk = nullptr;
		beg = beg_.load();
		do {
			end = end_.load();

			if (beg >= end) {
				beg = beg_.load();
				continue;
			}

			slot = beg % chunks_num_;
			chunk = queue_[slot];
			if (chunk == nullptr) {
				beg = beg_.load();
				continue;
			}

		} while(beg_.compare_exchange_weak(beg, beg + 1));
		queue_[slot].store(nullptr);
		return chunk;
	}

	void put_chunk(T* chunk) {
		::uint64_t beg, end;
		::uint64_t slot;
		end = end_.load();
		do {
			beg = beg_.load();

			if (end >= beg + chunks_num_) {
				end = end_.load();
				continue;
			}

			slot = end % chunks_num_;
			chunk = queue_[slot];
			if (chunk != nullptr) {
				end = end_.load();
				continue;
			}
		} while(end_.compare_exchange_weak(end, end + 1));
		queue_[slot].store(chunk);
	}

public:
	cache(const ::size_t chunk_size, const ::size_t min_chunks_num)
	: chunk_size_(chunk_size)
	, chunks_num_(M*min_chunks_num)
	, beg_(0)
	, end_(0)
	, queue_(new std::atomic<T*>[chunks_num_]) {
		assert(chunk_size_ > 0);
		assert(chunks_num_ > 0);

		std::fill(&queue_[0], &queue_[chunks_num_], nullptr);
	}

	void upkeep() {
		for (;;) {
			const ::uint64_t beg = beg_.load();
			const ::uint64_t end = end_.load();
			if (end > beg + (M-1)*chunks_num_/M) {
				delete[] get_chunk();
			} else if (end <= beg + chunks_num_/M) {
				put_chunk(new T[chunk_size_]);
			} else {
				break;
			}
		}
	}

	T* alloc() {
		return get_chunk();
	}

	void free(T* chunk) {
		put_chunk(chunk);
	}
};

} /* namespace rapidmem */
