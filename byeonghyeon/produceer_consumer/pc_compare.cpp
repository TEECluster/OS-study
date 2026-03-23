#include <iostream>
#include <thread>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <string>
#include <sstream>
#include <semaphore.h>   // POSIX semaphore (Linux / WSL)
#include <cstdlib>

using namespace std;

// ---------------------------------------------
// 공용 로그 함수
// 여러 스레드가 동시에 cout을 쓰면 출력이 섞일 수 있으므로
// 로그 출력 전용 mutex로 보호한다.
// ---------------------------------------------
mutex log_mtx;

void log_message(const string& msg) {
    lock_guard<mutex> lock(log_mtx);
    cout << msg << endl;
}

// 스레드가 너무 빠르게 돌아가면 로그가 너무 정신없으니
// 약간의 작업 시간처럼 보이게 sleep을 준다.
void small_sleep(int ms = 80) {
    this_thread::sleep_for(chrono::milliseconds(ms));
}

// 종료 신호용 특별 값
const int POISON_PILL = -1;

// ============================================================
// 1. Semaphore 기반 Bounded Buffer
// ============================================================
class SemaphoreBuffer {
private:
    deque<int> buffer;          // 공유 버퍼
    size_t capacity;            // 버퍼 최대 크기
    mutex mtx;                  // 버퍼 자체 보호용 mutex

    sem_t empty_slots;          // 비어 있는 칸 수
    sem_t full_slots;           // 채워진 칸 수

public:
    SemaphoreBuffer(size_t cap) : capacity(cap) {
        sem_init(&empty_slots, 0, cap); // 처음에는 버퍼가 전부 비어 있음
        sem_init(&full_slots, 0, 0);    // 처음에는 들어있는 데이터가 없음
    }

    ~SemaphoreBuffer() {
        sem_destroy(&empty_slots);
        sem_destroy(&full_slots);
    }

    // 생산자가 데이터 삽입
    void produce(int item, int producer_id) {
        // 버퍼가 가득 찼다면 여기서 block 된다.
        int empty_value = 0;
        sem_getvalue(&empty_slots, &empty_value);
        if (empty_value == 0) {
            log_message("[Semaphore][Producer " + to_string(producer_id) + "] buffer full -> block");
        }

        sem_wait(&empty_slots); // 빈 칸이 생길 때까지 대기

        {
            lock_guard<mutex> lock(mtx);
            buffer.push_back(item);

            stringstream ss;
            ss << "[Semaphore][Producer " << producer_id
               << "] produced " << item
               << " | buffer size = " << buffer.size();
            log_message(ss.str());
        }

        // 새로운 데이터가 들어왔으므로 소비자 하나를 깨울 수 있다.
        sem_post(&full_slots);
    }

    // 소비자가 데이터 꺼냄
    int consume(int consumer_id) {
        // 버퍼가 비어 있으면 여기서 block 된다.
        int full_value = 0;
        sem_getvalue(&full_slots, &full_value);
        if (full_value == 0) {
            log_message("[Semaphore][Consumer " + to_string(consumer_id) + "] buffer empty -> block");
        }

        sem_wait(&full_slots); // 데이터가 들어올 때까지 대기

        int item;
        {
            lock_guard<mutex> lock(mtx);
            item = buffer.front();
            buffer.pop_front();

            stringstream ss;
            ss << "[Semaphore][Consumer " << consumer_id
               << "] consumed " << item
               << " | buffer size = " << buffer.size();
            log_message(ss.str());
        }

        // 한 칸 비었으므로 생산자 하나를 깨울 수 있다.
        sem_post(&empty_slots);
        return item;
    }
};

// ============================================================
// 2. Condition Variable + Mutex 기반 Bounded Buffer
// ============================================================
class CVBuffer {
private:
    deque<int> buffer;
    size_t capacity;
    mutex mtx;
    condition_variable not_full;   // 버퍼가 꽉 찬 생산자 대기용
    condition_variable not_empty;  // 버퍼가 빈 소비자 대기용

public:
    CVBuffer(size_t cap) : capacity(cap) {}

    void produce(int item, int producer_id) {
        unique_lock<mutex> lock(mtx);

        // 버퍼가 가득 차 있으면 기다린다.
        while (buffer.size() == capacity) {
            log_message("[CV][Producer " + to_string(producer_id) + "] buffer full -> wait");
            not_full.wait(lock);
            log_message("[CV][Producer " + to_string(producer_id) + "] wake-up");
        }

        buffer.push_back(item);

        {
            stringstream ss;
            ss << "[CV][Producer " << producer_id
               << "] produced " << item
               << " | buffer size = " << buffer.size();
            log_message(ss.str());
        }

        // 데이터가 생겼으니 소비자에게 알림
        not_empty.notify_one();
    }

    int consume(int consumer_id) {
        unique_lock<mutex> lock(mtx);

        // 버퍼가 비어 있으면 기다린다.
        while (buffer.empty()) {
            log_message("[CV][Consumer " + to_string(consumer_id) + "] buffer empty -> wait");
            not_empty.wait(lock);
            log_message("[CV][Consumer " + to_string(consumer_id) + "] wake-up");
        }

        int item = buffer.front();
        buffer.pop_front();

        {
            stringstream ss;
            ss << "[CV][Consumer " << consumer_id
               << "] consumed " << item
               << " | buffer size = " << buffer.size();
            log_message(ss.str());
        }

        // 빈 칸이 생겼으니 생산자에게 알림
        not_full.notify_one();

        return item;
    }
};

// ============================================================
// Semaphore 방식용 Producer / Consumer
// ============================================================
void producer_semaphore(SemaphoreBuffer& shared_buffer, int producer_id, int items_per_producer) {
    for (int i = 0; i < items_per_producer; i++) {
        // 생산 데이터 구분을 쉽게 하기 위해 producer_id를 앞자리에 반영
        int item = producer_id * 1000 + i;
        small_sleep(50);
        shared_buffer.produce(item, producer_id);
    }

    log_message("[Semaphore][Producer " + to_string(producer_id) + "] finished");
}

void consumer_semaphore(SemaphoreBuffer& shared_buffer, int consumer_id, atomic<int>& consumed_count) {
    while (true) {
        small_sleep(100);
        int item = shared_buffer.consume(consumer_id);

        if (item == POISON_PILL) {
            log_message("[Semaphore][Consumer " + to_string(consumer_id) + "] received stop signal -> exit");
            break;
        }

        consumed_count++;
        small_sleep(120); // 소비 처리 시간
    }
}

// ============================================================
// CV 방식용 Producer / Consumer
// ============================================================
void producer_cv(CVBuffer& shared_buffer, int producer_id, int items_per_producer) {
    for (int i = 0; i < items_per_producer; i++) {
        int item = producer_id * 1000 + i;
        small_sleep(50);
        shared_buffer.produce(item, producer_id);
    }

    log_message("[CV][Producer " + to_string(producer_id) + "] finished");
}

void consumer_cv(CVBuffer& shared_buffer, int consumer_id, atomic<int>& consumed_count) {
    while (true) {
        small_sleep(100);
        int item = shared_buffer.consume(consumer_id);

        if (item == POISON_PILL) {
            log_message("[CV][Consumer " + to_string(consumer_id) + "] received stop signal -> exit");
            break;
        }

        consumed_count++;
        small_sleep(120);
    }
}

// ============================================================
// Semaphore 방식 실행
// ============================================================
void run_semaphore_mode(int buffer_size, int producer_count, int consumer_count, int items_per_producer) {
    log_message("=================================================");
    log_message("Running Semaphore mode");
    log_message("=================================================");

    SemaphoreBuffer shared_buffer(buffer_size);
    vector<thread> producers;
    vector<thread> consumers;
    atomic<int> consumed_count = 0;

    // 소비자 먼저 실행
    for (int i = 0; i < consumer_count; i++) {
        consumers.emplace_back(consumer_semaphore, ref(shared_buffer), i + 1, ref(consumed_count));
    }

    // 생산자 실행
    for (int i = 0; i < producer_count; i++) {
        producers.emplace_back(producer_semaphore, ref(shared_buffer), i + 1, items_per_producer);
    }

    // 생산자 종료 대기
    for (auto& t : producers) {
        t.join();
    }

    log_message("[Semaphore][Main] all producers finished");

    // 모든 소비자가 종료할 수 있도록 stop signal 삽입
    for (int i = 0; i < consumer_count; i++) {
        shared_buffer.produce(POISON_PILL, 0);
    }

    // 소비자 종료 대기
    for (auto& t : consumers) {
        t.join();
    }

    int total_items = producer_count * items_per_producer;

    log_message("[Semaphore][Main] total produced = " + to_string(total_items));
    log_message("[Semaphore][Main] total consumed = " + to_string(consumed_count.load()));
    log_message("[Semaphore][Main] finished\n");
}

// ============================================================
// CV 방식 실행
// ============================================================
void run_cv_mode(int buffer_size, int producer_count, int consumer_count, int items_per_producer) {
    log_message("=================================================");
    log_message("Running Condition Variable mode");
    log_message("=================================================");

    CVBuffer shared_buffer(buffer_size);
    vector<thread> producers;
    vector<thread> consumers;
    atomic<int> consumed_count = 0;

    for (int i = 0; i < consumer_count; i++) {
        consumers.emplace_back(consumer_cv, ref(shared_buffer), i + 1, ref(consumed_count));
    }

    for (int i = 0; i < producer_count; i++) {
        producers.emplace_back(producer_cv, ref(shared_buffer), i + 1, items_per_producer);
    }

    for (auto& t : producers) {
        t.join();
    }

    log_message("[CV][Main] all producers finished");

    for (int i = 0; i < consumer_count; i++) {
        shared_buffer.produce(POISON_PILL, 0);
    }

    for (auto& t : consumers) {
        t.join();
    }

    int total_items = producer_count * items_per_producer;

    log_message("[CV][Main] total produced = " + to_string(total_items));
    log_message("[CV][Main] total consumed = " + to_string(consumed_count.load()));
    log_message("[CV][Main] finished\n");
}

// ============================================================
// main
// 사용법:
// ./pc_compare semaphore 5 2 2 8
// ./pc_compare cv        5 2 2 8
// ============================================================
int main(int argc, char* argv[]) {
    if (argc != 6) {
        cout << "Usage: " << argv[0]
             << " [semaphore|cv] [buffer_size] [producer_count] [consumer_count] [items_per_producer]\n";
        return 1;
    }

    string mode = argv[1];
    int buffer_size = atoi(argv[2]);
    int producer_count = atoi(argv[3]);
    int consumer_count = atoi(argv[4]);
    int items_per_producer = atoi(argv[5]);

    if (buffer_size <= 0 || producer_count <= 0 || consumer_count <= 0 || items_per_producer <= 0) {
        cout << "All numeric arguments must be positive.\n";
        return 1;
    }

    if (mode == "semaphore") {
        run_semaphore_mode(buffer_size, producer_count, consumer_count, items_per_producer);
    } else if (mode == "cv") {
        run_cv_mode(buffer_size, producer_count, consumer_count, items_per_producer);
    } else {
        cout << "Invalid mode. Use 'semaphore' or 'cv'.\n";
        return 1;
    }

    return 0;
}