#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <string>

using namespace std;
using namespace std::chrono;

long long counter = 0;   // 공유 자원

mutex mtx;               // mutex lock
atomic_flag spin = ATOMIC_FLAG_INIT; // spinlock

void lock_spin() {
    while (spin.test_and_set(memory_order_acquire)) {
        // busy waiting
    }
}

void unlock_spin() {
    spin.clear(memory_order_release);
}

void worker_no_lock(int iterations) {
    for (int i = 0; i < iterations; i++) {
        counter++; // race condition 발생 가능
    }
}

void worker_mutex(int iterations) {
    for (int i = 0; i < iterations; i++) {
        lock_guard<mutex> lock(mtx);
        counter++;
    }
}

void worker_spinlock(int iterations) {
    for (int i = 0; i < iterations; i++) {
        lock_spin();
        counter++;
        unlock_spin();
    }
}

int main() {
    vector<int> thread_counts = {2, 4, 8};
    int iterations = 1000000;

    for (int tc : thread_counts) {
        cout << "\n=============================\n";
        cout << "Thread count: " << tc << "\n";
        cout << "Iterations per thread: " << iterations << "\n";
        cout << "Expected result: " << 1LL * tc * iterations << "\n";

        vector<string> modes = {"no_lock", "mutex", "spinlock"};

        for (const string& mode : modes) {
            counter = 0;
            vector<thread> threads;

            auto start = high_resolution_clock::now();

            for (int i = 0; i < tc; i++) {
                if (mode == "no_lock") {
                    threads.emplace_back(worker_no_lock, iterations);
                } else if (mode == "mutex") {
                    threads.emplace_back(worker_mutex, iterations);
                } else if (mode == "spinlock") {
                    threads.emplace_back(worker_spinlock, iterations);
                }
            }

            for (auto& t : threads) {
                t.join();
            }

            auto end = high_resolution_clock::now();
            auto elapsed = duration_cast<milliseconds>(end - start).count();

            cout << "[" << mode << "] "
                 << "result = " << counter
                 << ", time = " << elapsed << " ms";

            if (counter != 1LL * tc * iterations) {
                cout << "  <-- WRONG (Race Condition)";
            } else {
                cout << "  <-- CORRECT";
            }
            cout << "\n";
        }
    }

    return 0;
}