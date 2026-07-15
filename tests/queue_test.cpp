#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "sipdemo/BoundedQueue.h"

using namespace sipdemo;

TEST(BoundedQueue, BasicPushPop) {
    BoundedQueue<int> q(4);
    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_EQ(q.size(), 2u);
    EXPECT_EQ(*q.pop(), 1);
    EXPECT_EQ(*q.pop(), 2);
}

TEST(BoundedQueue, TryPushRespectsCapacity) {
    BoundedQueue<int> q(2);
    EXPECT_TRUE(q.tryPush(1));
    EXPECT_TRUE(q.tryPush(2));
    EXPECT_FALSE(q.tryPush(3));  // full
    EXPECT_EQ(*q.tryPop(), 1);
    EXPECT_TRUE(q.tryPush(3));   // space again
}

TEST(BoundedQueue, TryPopEmptyReturnsNullopt) {
    BoundedQueue<int> q(2);
    EXPECT_FALSE(q.tryPop().has_value());
}

TEST(BoundedQueue, CloseWakesConsumerAndDrains) {
    BoundedQueue<int> q(4);
    q.push(7);
    q.close();
    EXPECT_EQ(*q.pop(), 7);            // drains remaining
    EXPECT_FALSE(q.pop().has_value()); // then reports closed
    EXPECT_FALSE(q.push(8));           // push after close fails
}

// Producer/consumer stress. Run under ThreadSanitizer for the data-race check
// (cmake -DSIPDEMO_SANITIZE_THREAD=ON on a non-MSVC toolchain).
TEST(BoundedQueue, ConcurrentProducerConsumer) {
    BoundedQueue<int> q(8);
    constexpr int kCount = 100000;
    std::atomic<long long> sum{0};

    std::thread consumer([&] {
        while (auto v = q.pop()) sum += *v;
    });

    std::thread producer([&] {
        for (int i = 1; i <= kCount; ++i) q.push(i);
        q.close();
    });

    producer.join();
    consumer.join();

    long long expected = static_cast<long long>(kCount) * (kCount + 1) / 2;
    EXPECT_EQ(sum.load(), expected);
}

TEST(BoundedQueue, MultipleProducersConsumers) {
    BoundedQueue<int> q(16);
    constexpr int kPerProducer = 20000;
    constexpr int kProducers = 3;
    std::atomic<long long> total{0};
    std::atomic<int> consumedCount{0};

    std::vector<std::thread> consumers;
    for (int c = 0; c < 2; ++c) {
        consumers.emplace_back([&] {
            while (auto v = q.pop()) {
                total += *v;
                ++consumedCount;
            }
        });
    }
    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&] {
            for (int i = 0; i < kPerProducer; ++i) q.push(1);
        });
    }
    for (auto& t : producers) t.join();
    q.close();
    for (auto& t : consumers) t.join();

    EXPECT_EQ(consumedCount.load(), kProducers * kPerProducer);
    EXPECT_EQ(total.load(), static_cast<long long>(kProducers) * kPerProducer);
}
