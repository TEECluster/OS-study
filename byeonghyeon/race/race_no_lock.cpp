#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <cstdlib>

using namespace std;

// 공유 카운터
long long counter = 0;

// 스레드가 수행할 작업
void increment_counter(int iterations) {
    for (int i = 0; i < iterations; i++) {
        // 동기화 없이 바로 증가
        counter++;
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cout << "Usage: " << argv[0] << " [thread_count] [iterations]\n";
        cout << "Example: " << argv[0] << " 4 1000000\n";
        return 1;
    }

    int thread_count = atoi(argv[1]);
    int iterations = atoi(argv[2]);

    if (thread_count <= 0 || iterations <= 0) {
        cout << "Arguments must be positive integers.\n";
        return 1;
    }

    vector<thread> threads;

    long long expected = 1LL * thread_count * iterations;

    auto start = chrono::high_resolution_clock::now();

    for (int i = 0; i < thread_count; i++) {
        threads.emplace_back(increment_counter, iterations);
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end = chrono::high_resolution_clock::now();
    auto elapsed = chrono::duration_cast<chrono::microseconds>(end - start).count();

    cout << "=== No Lock Version ===\n";
    cout << "Thread count : " << thread_count << "\n";
    cout << "Iterations   : " << iterations << "\n";
    cout << "Expected     : " << expected << "\n";
    cout << "Actual       : " << counter << "\n";
    cout << "Correct?     : " << (counter == expected ? "YES" : "NO") << "\n";
    cout << "Time(us)     : " << elapsed << "\n";

    return 0;
}