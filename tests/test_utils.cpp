#include <gtest/gtest.h>
#include "utils/thread_pool.hpp"
#include <future>
#include <chrono>

using namespace nuc_display::utils;

TEST(ThreadPoolTest, SimpleTask) {
    ThreadPool pool(2);
    auto fut = pool.enqueue([]() { return 42; });
    EXPECT_EQ(fut.get(), 42);
}

TEST(ThreadPoolTest, MultipleTasks) {
    ThreadPool pool(4);
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 10; ++i) {
        futures.push_back(pool.enqueue([i]() { return i * i; }));
    }
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(futures[i].get(), i * i);
    }
}

TEST(ThreadPoolTest, StopAndQueue) {
    auto pool = std::make_unique<ThreadPool>(1);
    pool.reset(); // Destructor called, pool stopped.
}
