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
		::uint64_t slot;
		T* chunk = nullptr;
		::uint64_t beg = beg_.load(std::memory_order_relaxed);
		::uint64_t end = end_.load(std::memory_order_relaxed);
		for (;;) {
			::uint64_t x = beg;
			for (; x < end; ++x) {
				slot = x % chunks_num_;
				chunk = queue_[slot].load(std::memory_order_relaxed);
				if (chunk)
					break;
			}

			if (x >= end) {
				beg = beg_.load(std::memory_order_relaxed);
				end = end_.load(std::memory_order_relaxed);
				continue;
			}

			if (x > beg && !beg_.compare_exchange_strong(beg, x)) {
				end = end_.load(std::memory_order_relaxed);
				continue;
			}

			if (queue_[slot].compare_exchange_strong(chunk, nullptr)) {
				return chunk;
			} else {
				beg = beg_.load(std::memory_order_relaxed);
				end = end_.load(std::memory_order_relaxed);
				continue;
			}
		}
	}

	void put_chunk(T* chunk) {
		::uint64_t slot;
		T* prev_chunk = nullptr;
		::uint64_t beg = beg_.load(std::memory_order_relaxed);
		::uint64_t end = end_.load(std::memory_order_relaxed);
		for (;;) {
			::uint64_t y = end;
			for (; y < beg + chunks_num_; ++y) {
				slot = y % chunks_num_;
				prev_chunk = queue_[slot].load(std::memory_order_relaxed);
				if (!prev_chunk)
					break;
			}

			if (y >= beg + chunks_num_) {
				beg = beg_.load(std::memory_order_relaxed);
				end = end_.load(std::memory_order_relaxed);
				continue;
			}

			if (y > end && !end_.compare_exchange_strong(end, y)) {
				beg = beg_.load(std::memory_order_relaxed);
				continue;
			}

		 	if (queue_[slot].compare_exchange_strong(prev_chunk, chunk)) { // prev_chunk == nullptr
				return;
			} else {
				beg = beg_.load(std::memory_order_relaxed);
				end = end_.load(std::memory_order_relaxed);
				continue;
			}
		}
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
			const ::uint64_t beg = beg_.load(std::memory_order_relaxed);
			const ::uint64_t end = end_.load(std::memory_order_relaxed);
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
