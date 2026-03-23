#include <iostream>
#include <thread>
#include <queue>
#include <mutex>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <string>
#include <semaphore.h>

using namespace std;

// ============================================================
// 여러 스레드가 동시에 cout을 쓰면 로그가 섞일 수 있으므로
// 출력용 mutex를 따로 둔다.
// ============================================================
mutex print_mtx;

// 안전한 로그 출력 함수
void safe_print(const string& msg) {
    lock_guard<mutex> lock(print_mtx);
    cout << msg << endl;
}

// ============================================================
// Consumer 종료용 특별 값
// Producer가 모두 끝난 뒤 main에서 이 값을 넣어 주면
// Consumer가 종료할 수 있다.
// ============================================================
const int POISON_PILL = -1;

// ============================================================
// 세마포어 기반 공유 버퍼 클래스
//
// empty_slots : 현재 비어 있는 칸 수
// full_slots  : 현재 들어 있는 데이터 수
// mtx         : 실제 queue 접근 보호
// ============================================================
class Buffer {
private:
    queue<int> q;          // 실제 공유 버퍼
    int capacity;          // 최대 크기

    mutex mtx;             // queue 보호용 mutex
    sem_t empty_slots;     // 빈 칸 수
    sem_t full_slots;      // 채워진 칸 수

public:
    // 생성자
    Buffer(int size) : capacity(size) {
        // 처음에는 버퍼가 비어 있으므로
        // empty_slots = capacity
        // full_slots = 0
        sem_init(&empty_slots, 0, capacity);
        sem_init(&full_slots, 0, 0);
    }

    // 소멸자
    ~Buffer() {
        sem_destroy(&empty_slots);
        sem_destroy(&full_slots);
    }

    // --------------------------------------------------------
    // Producer가 데이터를 넣는 함수
    // --------------------------------------------------------
    void produce(int item, int producer_id) {
        // sem_wait 전에 현재 값을 확인해서
        // 로그를 보기 좋게 남긴다.
        int empty_value = 0;
        sem_getvalue(&empty_slots, &empty_value);

        // 빈 칸이 0개면 꽉 찬 상태
        if (empty_value == 0) {
            safe_print("[Producer " + to_string(producer_id) + "] buffer full -> wait");
        }

        // 빈 칸이 생길 때까지 기다림
        sem_wait(&empty_slots);

        // 실제 queue 접근은 mutex로 보호
        {
            lock_guard<mutex> lock(mtx);
            q.push(item);

            if (item == POISON_PILL) {
                safe_print("[Main] inserted STOP signal | buffer size = " + to_string(q.size()));
            } else {
                safe_print("[Producer " + to_string(producer_id) +
                           "] produced: " + to_string(item) +
                           " | buffer size = " + to_string(q.size()));
            }
        }

        // 데이터가 1개 생겼으므로 Consumer가 꺼낼 수 있게 알림
        sem_post(&full_slots);
    }

    // --------------------------------------------------------
    // Consumer가 데이터를 꺼내는 함수
    // --------------------------------------------------------
    int consume(int consumer_id) {
        // full_slots가 0이면 꺼낼 데이터가 없음
        int full_value = 0;
        sem_getvalue(&full_slots, &full_value);

        if (full_value == 0) {
            safe_print("[Consumer " + to_string(consumer_id) + "] buffer empty -> wait");
        }

        // 데이터가 생길 때까지 기다림
        sem_wait(&full_slots);

        int item;

        // queue 접근은 mutex로 보호
        {
            lock_guard<mutex> lock(mtx);

            item = q.front();
            q.pop();

            if (item == POISON_PILL) {
                safe_print("[Consumer " + to_string(consumer_id) +
                           "] received STOP signal | buffer size = " + to_string(q.size()));
            } else {
                safe_print("[Consumer " + to_string(consumer_id) +
                           "] consumed: " + to_string(item) +
                           " | buffer size = " + to_string(q.size()));
            }
        }

        // 빈 칸이 하나 생겼으므로 Producer가 넣을 수 있게 알림
        sem_post(&empty_slots);

        return item;
    }
};

// ============================================================
// Producer 스레드 함수
// ============================================================
void producer(Buffer& buffer, int producer_id, int items_to_produce) {
    for (int i = 0; i < items_to_produce; i++) {
        // 로그 보기 좋게 약간 대기
        this_thread::sleep_for(chrono::milliseconds(100));

        // Producer별 데이터 구분
        // 예: 1번 Producer -> 100, 101, ...
        //     2번 Producer -> 200, 201, ...
        int item = producer_id * 100 + i;

        buffer.produce(item, producer_id);
    }

    safe_print("[Producer " + to_string(producer_id) + "] finished");
}

// ============================================================
// Consumer 스레드 함수
// STOP 신호를 받을 때까지 계속 소비
// ============================================================
void consumer(Buffer& buffer, int consumer_id) {
    while (true) {
        // 소비 속도 약간 조절
        this_thread::sleep_for(chrono::milliseconds(150));

        int item = buffer.consume(consumer_id);

        // STOP 신호를 받으면 종료
        if (item == POISON_PILL) {
            break;
        }

        // 실제 처리 로그
        safe_print("[Consumer " + to_string(consumer_id) +
                   "] processed item: " + to_string(item));
    }

    safe_print("[Consumer " + to_string(consumer_id) + "] finished");
}

// ============================================================
// main
// 사용법:
// ./pc_sem [buffer_size] [producer_count] [consumer_count] [items_per_producer]
//
// 예:
// ./pc_sem 3 2 2 5
// ============================================================
int main(int argc, char* argv[]) {
    if (argc != 5) {
        cout << "Usage: " << argv[0]
             << " [buffer_size] [producer_count] [consumer_count] [items_per_producer]\n";
        cout << "Example: " << argv[0] << " 3 2 2 5\n";
        return 1;
    }

    int buffer_size = atoi(argv[1]);
    int producer_count = atoi(argv[2]);
    int consumer_count = atoi(argv[3]);
    int items_per_producer = atoi(argv[4]);

    if (buffer_size <= 0 || producer_count <= 0 || consumer_count <= 0 || items_per_producer <= 0) {
        cout << "All arguments must be positive integers.\n";
        return 1;
    }

    Buffer buffer(buffer_size);

    vector<thread> producers;
    vector<thread> consumers;

    safe_print("==================================================");
    safe_print("Producer-Consumer (Semaphore)");
    safe_print("buffer_size        = " + to_string(buffer_size));
    safe_print("producer_count     = " + to_string(producer_count));
    safe_print("consumer_count     = " + to_string(consumer_count));
    safe_print("items_per_producer = " + to_string(items_per_producer));
    safe_print("==================================================");

    // Consumer 먼저 시작
    for (int i = 0; i < consumer_count; i++) {
        consumers.emplace_back(consumer, ref(buffer), i + 1);
    }

    // Producer 시작
    for (int i = 0; i < producer_count; i++) {
        producers.emplace_back(producer, ref(buffer), i + 1, items_per_producer);
    }

    // Producer 종료 대기
    for (auto& t : producers) {
        t.join();
    }

    safe_print("[Main] all producers finished");

    // Consumer 수만큼 STOP 신호 삽입
    for (int i = 0; i < consumer_count; i++) {
        buffer.produce(POISON_PILL, 0);
    }

    // Consumer 종료 대기
    for (auto& t : consumers) {
        t.join();
    }

    safe_print("==================================================");
    safe_print("[Main] all threads finished successfully");
    safe_print("==================================================");

    return 0;
}