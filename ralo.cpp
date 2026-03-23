#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <iomanip>

using namespace std;

// 공유 자원 (Shared Counter)
int shared_counter = 0;

// 동기화 도구
mutex mtx;
//스핀락을 구현하기 위한 명령어로 원자적 상태를 보장하며 현재는 clear상태
atomic_flag spinlock_flag = ATOMIC_FLAG_INIT;

// 락 종류 열거형
enum LockType { NO_LOCK, MUTEX, SPINLOCK };

// 스레드가 수행할 작업 (카운터 증가)
void increment_counter(LockType lock_type, int iterations) {
    for (int i = 0; i < iterations; ++i) {
        if (lock_type == NO_LOCK) {
            // 1. 동기화 없음 (Race Condition 발생)
            shared_counter++; 
        } 
        else if (lock_type == MUTEX) {
            // 2. Mutex 기반 보호 (명시적 Lock/Unlock 사용)
            mtx.lock();       
            shared_counter++; 
            mtx.unlock();     
        } 
        else if (lock_type == SPINLOCK) {
            // 3. Spinlock 기반 보호
            while (spinlock_flag.test_and_set(memory_order_acquire)) {
                // Lock을 획득할 때까지 대기 (Spin)
            }
            shared_counter++;
            spinlock_flag.clear(memory_order_release);
        }
    }
}

// 실험 실행 및 측정 함수
void run_experiment(string test_name, LockType lock_type, int num_threads, int iterations, int repeats) {
    cout << "====================================================\n";
    cout << "[테스트] " << test_name << "\n";
    cout << "- 스레드 수: " << num_threads << ", 반복 횟수(스레드당): " << iterations << "\n";
    
    int expected_value = num_threads * iterations;

    for (int r = 1; r <= repeats; ++r) {
        shared_counter = 0; // 카운터 초기화
        vector<thread> threads;

        // 실행 시간 측정 시작
        auto start_time = chrono::high_resolution_clock::now();

        // 스레드 생성 및 실행
        for (int i = 0; i < num_threads; ++i) {
            threads.push_back(thread(increment_counter, lock_type, iterations));
        }

        // 모든 스레드 종료 대기
        for (auto& t : threads) {
            t.join();
        }

        // 실행 시간 측정 종료
        auto end_time = chrono::high_resolution_clock::now();
        chrono::duration<double, milli> duration = end_time - start_time;

        // 오류 여부 확인
        bool is_error = (shared_counter != expected_value);

        // 결과 출력
        cout << "  [실행 " << r << "] "
             << "결과값: " << setw(7) << shared_counter 
             << " (기대값: " << expected_value << ") | "
             << "시간: " << fixed << setprecision(2) << setw(6) << duration.count() << " ms | "
             << "오류: " << (is_error ? "O (Race Condition)" : "X (정상)") << "\n";
    }
    cout << "====================================================\n\n";
}

int main() {
    // 실험 변수 설정 (스레드 수, 스레드당 반복 횟수, 반복 측정 횟수)
    int threads = 10;
    int iterations = 100000; // Race condition을 확실히 보기 위해 큰 값 설정
    int repeats = 5;         // 요구사항: 최소 5회 반복

    // 1. 동기화 없이 실행 (잘못된 결과가 발생하는 사례 확인)
    run_experiment("동기화 없음 (No Lock)", NO_LOCK, threads, iterations, repeats);

    // 2. Mutex 적용 (수정 후 정상화된 결과 확인)
    run_experiment("Mutex 적용", MUTEX, threads, iterations, repeats);

    // 3. Spinlock 적용 (Mutex와의 성능/대기 특성 비교)
    run_experiment("Spinlock 적용", SPINLOCK, threads, iterations, repeats);

    // 4. 요소 변경 실험 (스레드 수를 줄이고 반복 횟수를 늘림)
    run_experiment("Mutex (스레드 축소, 반복 증가)", MUTEX, 4, 250000, 3);

    return 0;
}