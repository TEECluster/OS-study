#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <ctime>
#include <string>

using namespace std;

class SpinLock {
private:
    atomic_flag flag = ATOMIC_FLAG_INIT;

public:
    void lock(long long& retryCount) {
        while (flag.test_and_set(memory_order_acquire)) {
            retryCount++;
        }
    }

    void unlock() {
        flag.clear(memory_order_release);
    }
};

int main() {
    int threadCount;
    long long repeatCount;
    string mode;

    cout << "스레드 수 입력: ";
    cin >> threadCount;

    cout << "각 스레드 반복 횟수 입력: ";
    cin >> repeatCount;

    cout << "모드 입력 (none / mutex / spin): ";
    cin >> mode;

    long long counter = 0;
    mutex mtx;
    SpinLock spin;

    vector<thread> threads;
    vector<long long> retryCounts(threadCount, 0);

    auto wallStart = chrono::high_resolution_clock::now();
    clock_t cpuStart = clock();

    for (int i = 0; i < threadCount; i++) {
        threads.push_back(thread([&, i]() {
            for (long long j = 0; j < repeatCount; j++) {
                if (mode == "none") {
                    counter++;
                }
                else if (mode == "mutex") {
                    mtx.lock();
                    counter++;
                    mtx.unlock();
                }
                else if (mode == "spin") {
                    spin.lock(retryCounts[i]);
                    counter++;
                    spin.unlock();
                }
            }
        }));
    }

    for (int i = 0; i < threadCount; i++) {
        threads[i].join();
    }

    clock_t cpuEnd = clock();
    auto wallEnd = chrono::high_resolution_clock::now();

    long long expected = (long long)threadCount * repeatCount;
    double wallTime = chrono::duration<double, milli>(wallEnd - wallStart).count();
    double cpuTime = (double)(cpuEnd - cpuStart) / CLOCKS_PER_SEC * 1000.0;

    cout << "\n===== 실행 결과 =====\n";
    cout << "모드              : " << mode << '\n';
    cout << "스레드 수         : " << threadCount << '\n';
    cout << "반복 횟수         : " << repeatCount << '\n';
    cout << "기대값            : " << expected << '\n';
    cout << "실제 counter 값   : " << counter << '\n';
    cout << "실행 시간(ms)     : " << wallTime << '\n';
    cout << "CPU 시간(ms)      : " << cpuTime << '\n';

    if (mode == "spin") {
        long long totalRetry = 0;
        for (int i = 0; i < threadCount; i++) {
            totalRetry += retryCounts[i];
        }
        cout << "총 spin 재시도 수 : " << totalRetry << '\n';
    }

    if (counter != expected) {
        cout << "결과 판정         : Race Condition 발생 가능\n";
    }
    else {
        cout << "결과 판정         : 정상 동기화\n";
    }

    return 0;
}