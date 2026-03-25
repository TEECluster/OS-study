#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <string>

using namespace std;
using namespace std::chrono;

// 공유 변수 (모든 스레드가 접근)
long long counter_value = 0;

// Mutex (sleep 기반 lock)
mutex mtx;

// SpinLock (busy waiting 기반 lock)
class SpinLock {
private:
    atomic_flag flag = ATOMIC_FLAG_INIT; // lock 상태 저장

public:
    void lock() {
        // 이미 lock이면 계속 반복 (CPU 사용하면서 대기)
        while (flag.test_and_set(memory_order_acquire)) {
        }
    }

    void unlock() {
        flag.clear(memory_order_release); // lock 해제
    }
};

SpinLock spin;


// 동기화 없음 (Race Condition 발생)
void worker_no_lock(int iterations) {
    for (int i = 0; i < iterations; i++) {
        counter_value++; // 보호 없이 접근 → 데이터 깨짐 가능
    }
}

// Mutex 사용
void worker_mutex(int iterations) {
    for (int i = 0; i < iterations; i++) {
        mtx.lock();          // lock 획득
        counter_value++;     // 임계구역 (Critical Section)
        mtx.unlock();        // lock 해제
    }
}

// SpinLock 사용
void worker_spinlock(int iterations) {
    for (int i = 0; i < iterations; i++) {
        spin.lock();         // lock 획득 (busy wait)
        counter_value++;     // 임계구역
        spin.unlock();       // lock 해제
    }
}

// 실험 결과 저장 구조체
struct ExperimentResult {
    string mode;        // 실행 모드 (no_lock, mutex, spinlock)
    int threads;        // 스레드 수
    int iterations;     // 반복 횟수
    long long expected; // 기대값
    long long actual;   // 실제 결과
    long long elapsed_us; // 실행 시간
    bool correct;       // 결과 일치 여부
};


// 실험 실행 함수
ExperimentResult run_experiment(const string& mode, int num_threads, int iterations) {
    counter_value = 0; // 초기화

    vector<thread> workers;

    // 시간 측정 시작
    auto start = high_resolution_clock::now();

    // 스레드 생성
    for (int i = 0; i < num_threads; i++) {
        if (mode == "no_lock") {
            workers.emplace_back(worker_no_lock, iterations);
        } else if (mode == "mutex") {
            workers.emplace_back(worker_mutex, iterations);
        } else if (mode == "spinlock") {
            workers.emplace_back(worker_spinlock, iterations);
        }
    }

    // 모든 스레드 종료 대기
    for (auto& t : workers) {
        t.join();
    }

    // 시간 측정 종료
    auto end = high_resolution_clock::now();
    auto elapsed = duration_cast<microseconds>(end - start).count();

    // 기대값 계산
    long long expected = 1LL * num_threads * iterations;

    return {
        mode,
        num_threads,
        iterations,
        expected,
        counter_value,
        elapsed,
        counter_value == expected
    };
}

// 결과 출력
void print_result(const ExperimentResult& r, int round) {
    cout << "[Run " << round << "] "
         << "Mode=" << r.mode
         << ", Threads=" << r.threads
         << ", Iterations=" << r.iterations
         << ", Expected=" << r.expected
         << ", Actual=" << r.actual
         << ", Time(us)=" << r.elapsed_us
         << ", Correct=" << (r.correct ? "YES" : "NO")
         << '\n';
}


 // 메인 함수
int main() {
    vector<int> thread_counts = {2, 4, 8};        // 스레드 수 변화
    vector<int> iteration_counts = {100000, 500000}; // 반복 횟수 변화
    vector<string> modes = {"no_lock", "mutex", "spinlock"};

    for (int threads : thread_counts) {
        for (int iterations : iteration_counts) {

            cout << "\n========================================\n";
            cout << "Experiment: Threads=" << threads
                 << ", Iterations=" << iterations << '\n';
            cout << "========================================\n";

            // 각 모드별 실행
            for (const auto& mode : modes) {
                for (int run = 1; run <= 5; run++) {
                    ExperimentResult result = run_experiment(mode, threads, iterations);
                    print_result(result, run);
                }
                cout << '\n';
            }
        }
    }

    return 0;
}