#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <string>

using namespace std;

// 실험 설정값
const int BUFFER_SIZE = 5;          // 공유 버퍼 크기
const int NUM_PRODUCERS = 2;        // Producer 개수
const int NUM_CONSUMERS = 2;        // Consumer 개수
const int ITEMS_PER_PRODUCER = 10;  // 각 Producer가 생산할 아이템 수

// 공유 버퍼
vector<int> buffer(BUFFER_SIZE, 0); // 원형 버퍼
int in = 0;                         // 다음 삽입 위치
int out = 0;                        // 다음 제거 위치
int count = 0;                      // 현재 버퍼에 들어있는 데이터 개수

// 동기화 도구
mutex mtx;                          // 버퍼 보호용 mutex
condition_variable not_full;        // 버퍼가 꽉 찼을 때 Producer 대기
condition_variable not_empty;       // 버퍼가 비었을 때 Consumer 대기

// 출력이 섞이지 않도록 하는 mutex
mutex print_mtx;

// 안전한 출력 함수
// 여러 스레드가 동시에 출력해도 줄이 섞이지 않게 보호
void safePrint(const string& msg) {
    lock_guard<mutex> lock(print_mtx);
    cout << msg << '\n';
}

// 버퍼 상태 출력
// 전역 buffer를 직접 읽지 않고, 복사본(snapshot)을 받아 출력
void printBufferSnapshot(const vector<int>& snapshot, int snapshot_count) {
    lock_guard<mutex> lock(print_mtx);

    cout << "    [buffer] ";
    for (int v : snapshot) {
        cout << v << " ";
    }
    cout << " | count = " << snapshot_count << '\n';
}

// Producer
void producer(int id) {
    for (int i = 0; i < ITEMS_PER_PRODUCER; i++) {
        int item = id * 100 + i; // 각 Producer마다 다른 값 생성

        string log_msg;          // 출력용 메시지
        vector<int> snapshot;    // 버퍼 상태 복사본
        int snapshot_count;

        unique_lock<mutex> lock(mtx);

        // 버퍼가 꽉 찼으면 대기
        while (count == BUFFER_SIZE) {
            lock.unlock(); // 출력 위해 잠시 해제
            safePrint("[Producer " + to_string(id) + "] buffer full -> block");
            lock.lock();

            // 조건 만족할 때까지 sleep (busy waiting X)
            not_full.wait(lock, [] { return count < BUFFER_SIZE; });

            lock.unlock();
            safePrint("[Producer " + to_string(id) + "] wake-up (space available)");
            lock.lock();
        }

        // 임계구역: 공유 데이터 수정
        int insert_index = in;
        buffer[in] = item;
        in = (in + 1) % BUFFER_SIZE; // 원형 버퍼
        count++;

        // 출력용 데이터는 미리 복사
        snapshot = buffer;
        snapshot_count = count;

        log_msg = "[Producer " + to_string(id) + "] produced " +
                  to_string(item) + " at index " + to_string(insert_index);

        lock.unlock(); // 임계구역 종료

        // 출력은 락 밖에서
        safePrint(log_msg);
        printBufferSnapshot(snapshot, snapshot_count);

        // Consumer 하나 깨우기
        not_empty.notify_one();

        this_thread::sleep_for(chrono::milliseconds(100));
    }
}

// Consumer
void consumer(int id, int items_to_consume) {
    for (int i = 0; i < items_to_consume; i++) {
        string log_msg;
        vector<int> snapshot;
        int snapshot_count;

        unique_lock<mutex> lock(mtx);

        // 버퍼가 비어 있으면 대기
        while (count == 0) {
            lock.unlock();
            safePrint("[Consumer " + to_string(id) + "] buffer empty -> block");
            lock.lock();

            not_empty.wait(lock, [] { return count > 0; });

            lock.unlock();
            safePrint("[Consumer " + to_string(id) + "] wake-up (item available)");
            lock.lock();
        }

        // 임계구역: 데이터 제거
        int remove_index = out;
        int item = buffer[out];
        buffer[out] = 0; // 보기 쉽게 초기화
        out = (out + 1) % BUFFER_SIZE;
        count--;

        // 출력용 데이터 복사
        snapshot = buffer;
        snapshot_count = count;

        log_msg = "[Consumer " + to_string(id) + "] consumed " +
                  to_string(item) + " at index " + to_string(remove_index);

        lock.unlock(); // 임계구역 종료

        // 출력은 락 밖에서
        safePrint(log_msg);
        printBufferSnapshot(snapshot, snapshot_count);

        // Producer 하나 깨우기
        not_full.notify_one();

        this_thread::sleep_for(chrono::milliseconds(150));
    }
}

// main
int main() {
    vector<thread> producers;
    vector<thread> consumers;

    // 전체 생산량
    int total_items = NUM_PRODUCERS * ITEMS_PER_PRODUCER;

    // Consumer에게 균등 분배
    int base_items = total_items / NUM_CONSUMERS;
    int extra = total_items % NUM_CONSUMERS;

    // Producer 생성
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        producers.emplace_back(producer, i);
    }

    // Consumer 생성
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

    safePrint("[main] finished successfully");
    return 0;
}