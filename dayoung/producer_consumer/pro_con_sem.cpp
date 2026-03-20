#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <semaphore>
#include <chrono>

using namespace std;

// 실험 설정값
const int BUFFER_SIZE = 5;          // 공유 버퍼 크기
const int NUM_PRODUCERS = 2;        // Producer 개수
const int NUM_CONSUMERS = 2;        // Consumer 개수
const int ITEMS_PER_PRODUCER = 10;  // 각 Producer가 생산할 아이템 수

// 공유 버퍼 관련 변수
vector<int> buffer(BUFFER_SIZE, 0); // 고정 크기 버퍼
int in = 0;                         // 다음 삽입 위치
int out = 0;                        // 다음 제거 위치

// 동기화 도구
// empty_slots: 비어 있는 칸의 개수
// full_slots : 들어 있는 데이터 개수
counting_semaphore<> empty_slots(BUFFER_SIZE);
counting_semaphore<> full_slots(0);

mutex buffer_mtx;   // 공유 버퍼 보호용 mutex
mutex print_mtx;    // 출력 섞임 방지용 mutex

// 버퍼 상태 출력 함수
void printBufferState() {
    lock_guard<mutex> lock(print_mtx);

    cout << "    [buffer] ";
    for (int i = 0; i < BUFFER_SIZE; i++) {
        cout << buffer[i] << " ";
    }
    cout << "\n";
}

// Producer 함수
void producer(int id) {
    for (int i = 0; i < ITEMS_PER_PRODUCER; i++) {
        int item = id * 100 + i; // Producer마다 구분되는 값 생성

        // 비어 있는 칸이 없으면 Producer는 여기서 대기
        if (!empty_slots.try_acquire()) {
            {
                lock_guard<mutex> print_lock(print_mtx);
                cout << "[Producer " << id << "] buffer full -> block\n";
            }
            empty_slots.acquire();
            {
                lock_guard<mutex> print_lock(print_mtx);
                cout << "[Producer " << id << "] wake-up (space available)\n";
            }
        }

        // 버퍼 수정은 한 번에 하나의 스레드만 가능
        {
            lock_guard<mutex> lock(buffer_mtx);

            buffer[in] = item;
            {
                lock_guard<mutex> print_lock(print_mtx);
                cout << "[Producer " << id << "] produced " << item
                     << " at index " << in << "\n";
            }

            in = (in + 1) % BUFFER_SIZE; // 원형 버퍼
            printBufferState();
        }

        // 데이터가 1개 생겼음을 Consumer에게 알림
        full_slots.release();

        this_thread::sleep_for(chrono::milliseconds(100));
    }
}

// Consumer 함수
void consumer(int id, int items_to_consume) {
    for (int i = 0; i < items_to_consume; i++) {
        // 소비할 데이터가 없으면 Consumer는 여기서 대기
        if (!full_slots.try_acquire()) {
            {
                lock_guard<mutex> print_lock(print_mtx);
                cout << "[Consumer " << id << "] buffer empty -> block\n";
            }
            full_slots.acquire();
            {
                lock_guard<mutex> print_lock(print_mtx);
                cout << "[Consumer " << id << "] wake-up (item available)\n";
            }
        }

        // 버퍼 수정은 한 번에 하나의 스레드만 가능
        {
            lock_guard<mutex> lock(buffer_mtx);

            int item = buffer[out];
            buffer[out] = 0; // 보기 쉽게 0으로 초기화

            {
                lock_guard<mutex> print_lock(print_mtx);
                cout << "[Consumer " << id << "] consumed " << item
                     << " at index " << out << "\n";
            }

            out = (out + 1) % BUFFER_SIZE; // 원형 버퍼
            printBufferState();
        }

        // 빈 칸이 1개 생겼음을 Producer에게 알림
        empty_slots.release();

        this_thread::sleep_for(chrono::milliseconds(150));
    }
}

// main 함수
int main() {
    vector<thread> producers;
    vector<thread> consumers;

    int total_items = NUM_PRODUCERS * ITEMS_PER_PRODUCER;
    int base_items = total_items / NUM_CONSUMERS;
    int extra = total_items % NUM_CONSUMERS;

    // Producer 스레드 생성
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        producers.emplace_back(producer, i);
    }

    // Consumer 스레드 생성
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        int items_to_consume = base_items + (i < extra ? 1 : 0);
        consumers.emplace_back(consumer, i, items_to_consume);
    }

    // 모든 Producer 종료 대기
    for (auto& t : producers) {
        t.join();
    }

    // 모든 Consumer 종료 대기
    for (auto& t : consumers) {
        t.join();
    }

    cout << "\n[main] finished successfully\n";
    return 0;
}