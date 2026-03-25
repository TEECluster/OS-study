#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <semaphore>
#include <chrono>
#include <string>

using namespace std;

// 실험 설정값
const int BUFFER_SIZE = 5;          // 공유 버퍼 크기
const int NUM_PRODUCERS = 2;        // Producer 개수
const int NUM_CONSUMERS = 2;        // Consumer 개수
const int ITEMS_PER_PRODUCER = 10;  // 각 Producer가 생산할 아이템 수

// 출력 보호용 mutex
// 여러 스레드가 동시에 cout을 쓰면 출력이 섞이므로 보호
mutex print_mtx;

// 한 줄 출력 함수 (출력 동기화)
void safePrint(const string& msg) {
    lock_guard<mutex> lock(print_mtx);
    cout << msg << '\n';
}

// 버퍼 상태 출력 함수
// 공유 버퍼를 직접 읽지 않고 복사본(snapshot)을 출력
void printBufferSnapshot(const vector<int>& snapshot) {
    lock_guard<mutex> lock(print_mtx);

    cout << "    [buffer] ";
    for (int value : snapshot) {
        cout << value << " ";
    }
    cout << '\n';
}

// 공유 버퍼 클래스
class BoundedBuffer {
private:
    vector<int> buffer;                // 실제 데이터를 저장하는 버퍼
    int in;                            // 다음 삽입 위치
    int out;                           // 다음 제거 위치

    // 세마포어
    counting_semaphore<> empty_slots;  // 비어 있는 칸 개수
    counting_semaphore<> full_slots;   // 채워진 칸 개수

    mutex buffer_mtx;                  // 버퍼 접근 보호용 mutex

public:
    // 생성자
    BoundedBuffer(int size)
        : buffer(size, 0),
          in(0),
          out(0),
          empty_slots(size),  // 처음엔 전부 비어 있음
          full_slots(0) {}    // 처음엔 데이터 없음

    // 생산 함수
    void produce(int producer_id, int item) {
        // 빈 칸이 생길 때까지 대기 (버퍼가 가득 차면 block)
        empty_slots.acquire();

        string log_msg;       // 출력 메시지
        vector<int> snapshot; // 출력용 버퍼 복사본

        {
            // 공유 데이터 접근 → mutex로 보호
            lock_guard<mutex> lock(buffer_mtx);

            int insert_index = in;        // 현재 위치 저장
            buffer[in] = item;            // 데이터 삽입
            in = (in + 1) % buffer.size(); // 원형 버퍼 처리

            // 출력 정보는 락 안에서 준비
            log_msg = "[Producer " + to_string(producer_id) + "] produced " +
                      to_string(item) + " at index " + to_string(insert_index);

            snapshot = buffer; // 버퍼 상태 복사
        }

        // 출력은 mutex 밖에서 수행 (임계구역 최소화)
        safePrint(log_msg);
        printBufferSnapshot(snapshot);

        // 데이터 1개 추가됨 → Consumer 깨움
        full_slots.release();
    }

    // 소비 함수
    void consume(int consumer_id) {
        // 데이터가 생길 때까지 대기 (버퍼가 비어 있으면 block)
        full_slots.acquire();

        string log_msg;
        vector<int> snapshot;

        {
            lock_guard<mutex> lock(buffer_mtx);

            int remove_index = out;       // 현재 위치 저장
            int item = buffer[out];       // 데이터 꺼냄
            buffer[out] = 0;              // 보기 쉽게 초기화
            out = (out + 1) % buffer.size(); // 원형 버퍼 처리

            log_msg = "[Consumer " + to_string(consumer_id) + "] consumed " +
                      to_string(item) + " at index " + to_string(remove_index);

            snapshot = buffer; // 버퍼 상태 복사
        }

        // 출력은 mutex 밖에서 수행
        safePrint(log_msg);
        printBufferSnapshot(snapshot);

        // 빈 칸 1개 생김 → Producer 깨움
        empty_slots.release();
    }
};

// Producer 스레드 
void producer(BoundedBuffer& shared_buffer, int id) {
    for (int i = 0; i < ITEMS_PER_PRODUCER; i++) {
        int item = id * 100 + i;  // Producer별 고유 값 생성
        shared_buffer.produce(id, item);

        // 실행 흐름 확인을 위한 지연
        this_thread::sleep_for(chrono::milliseconds(100));
    }
}

// Consumer 스레드
void consumer(BoundedBuffer& shared_buffer, int id, int items_to_consume) {
    for (int i = 0; i < items_to_consume; i++) {
        shared_buffer.consume(id);

        // 실행 흐름 확인을 위한 지연
        this_thread::sleep_for(chrono::milliseconds(150));
    }
}

// main
int main() {
    BoundedBuffer shared_buffer(BUFFER_SIZE); // 공유 버퍼 생성

    vector<thread> producers;
    vector<thread> consumers;

    // 총 생산 개수
    int total_items = NUM_PRODUCERS * ITEMS_PER_PRODUCER;

    // Consumer마다 나눠줄 개수 계산
    int base_items = total_items / NUM_CONSUMERS;
    int extra = total_items % NUM_CONSUMERS;

    // Producer 생성
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        producers.emplace_back(producer, ref(shared_buffer), i);
    }

    // Consumer 생성
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        int items_to_consume = base_items + (i < extra ? 1 : 0);
        consumers.emplace_back(consumer, ref(shared_buffer), i, items_to_consume);
    }

    // Producer 종료 대기
    for (auto& t : producers) {
        t.join();
    }

    // Consumer 종료 대기
    for (auto& t : consumers) {
        t.join();
    }

    safePrint("[main] finished successfully");
    return 0;
}