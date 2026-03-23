#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
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
int count = 0;                      // 현재 버퍼에 들어있는 아이템 수

// 동기화 도구
mutex mtx;                          // 공유 자원 보호용 mutex
condition_variable not_full;        // 버퍼가 가득 차지 않았음을 기다리는 조건변수
condition_variable not_empty;       // 버퍼가 비어 있지 않음을 기다리는 조건변수

// 로그 출력이 섞이지 않게 하기 위한 mutex
mutex print_mtx;

// 버퍼 상태 출력 함수
void printBufferState() {
    lock_guard<mutex> lock(print_mtx);

    cout << "    [buffer] ";
    for (int i = 0; i < BUFFER_SIZE; i++) {
        cout << buffer[i] << " ";
    }
    cout << " | count = " << count << "\n";
}

// Producer 함수
void producer(int id) {
    for (int i = 0; i < ITEMS_PER_PRODUCER; i++) {
        int item = id * 100 + i; // Producer마다 다른 값 생성

        // 공유 버퍼 접근 전 mutex 잠금
        unique_lock<mutex> lock(mtx);

        // 버퍼가 가득 찼다면 소비자가 꺼낼 때까지 대기
        while (count == BUFFER_SIZE) {
            {
                lock_guard<mutex> print_lock(print_mtx);
                cout << "[Producer " << id << "] buffer full -> block\n";
            }
            not_full.wait(lock); // lock을 잠시 풀고 대기, 깨어나면 다시 lock 획득
            {
                lock_guard<mutex> print_lock(print_mtx);
                cout << "[Producer " << id << "] wake-up (space available)\n";
            }
        }

        // 버퍼에 데이터 삽입
        buffer[in] = item;
        {
            lock_guard<mutex> print_lock(print_mtx);
            cout << "[Producer " << id << "] produced " << item
                 << " at index " << in << "\n";
        }

        in = (in + 1) % BUFFER_SIZE; // 원형 버퍼처럼 다음 위치로 이동
        count++;

        printBufferState();

        // 이제 버퍼가 비어 있지 않으므로 Consumer 하나 깨움
        not_empty.notify_one();

        // lock은 scope 벗어나면 자동 해제
        lock.unlock();

        // 실행 흐름이 너무 빨라 로그가 보기 힘들지 않도록 약간 sleep
        this_thread::sleep_for(chrono::milliseconds(100));
    }
}

// Consumer 함수
void consumer(int id, int items_to_consume) {
    for (int i = 0; i < items_to_consume; i++) {
        unique_lock<mutex> lock(mtx);

        // 버퍼가 비어 있으면 Producer가 넣을 때까지 대기
        while (count == 0) {
            {
                lock_guard<mutex> print_lock(print_mtx);
                cout << "[Consumer " << id << "] buffer empty -> block\n";
            }
            not_empty.wait(lock); // lock을 잠시 풀고 대기
            {
                lock_guard<mutex> print_lock(print_mtx);
                cout << "[Consumer " << id << "] wake-up (item available)\n";
            }
        }

        // 버퍼에서 데이터 꺼내기
        int item = buffer[out];
        buffer[out] = 0; // 보기 쉽게 0으로 초기화

        {
            lock_guard<mutex> print_lock(print_mtx);
            cout << "[Consumer " << id << "] consumed " << item
                 << " at index " << out << "\n";
        }

        out = (out + 1) % BUFFER_SIZE;
        count--;

        printBufferState();

        // 이제 버퍼에 빈 칸이 생겼으므로 Producer 하나 깨움
        not_full.notify_one();

        lock.unlock();

        this_thread::sleep_for(chrono::milliseconds(150));
    }
}


// main
int main() {
    vector<thread> producers;
    vector<thread> consumers;

    // 전체 생산 개수
    int total_items = NUM_PRODUCERS * ITEMS_PER_PRODUCER;

    // Consumer마다 소비할 개수 분배
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