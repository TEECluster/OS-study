#include <iostream>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <chrono>
#include <cstdlib>   // atoi
#include <string>

using namespace std;

// ============================================================
// 여러 스레드가 동시에 cout을 쓰면 출력이 섞일 수 있다.
// 그래서 출력 전용 mutex를 따로 둔다.
// ============================================================
mutex print_mtx;

// 안전하게 로그를 출력하는 함수
void safe_print(const string& msg) {
    lock_guard<mutex> lock(print_mtx);
    cout << msg << endl;
}

// ============================================================
// Consumer 종료를 위한 특별한 값
// 일반 생산 데이터와 구분하기 위해 -1 사용
// Producer가 모든 작업을 끝낸 뒤 이 값을 넣으면
// Consumer는 "이제 종료해야 하는구나" 하고 빠져나간다.
// ============================================================
const int POISON_PILL = -1;

// ============================================================
// 공유 버퍼 클래스
// Bounded Buffer 역할
// - queue<int> : 실제 데이터 저장
// - capacity   : 최대 크기
// - mutex      : 임계구역 보호
// - condition_variable 2개:
//      not_full  -> 버퍼가 꽉 찼을 때 Producer가 기다림
//      not_empty -> 버퍼가 비었을 때 Consumer가 기다림
// ============================================================
class Buffer {
private:
    queue<int> q;                   // 공유 버퍼
    int capacity;                   // 버퍼 최대 크기

    mutex mtx;                      // 버퍼 보호용 mutex
    condition_variable not_full;    // 버퍼에 자리가 생기길 기다리는 Producer용
    condition_variable not_empty;   // 버퍼에 데이터가 생기길 기다리는 Consumer용

public:
    // 생성자
    Buffer(int size) : capacity(size) {}

    // --------------------------------------------------------
    // Producer가 데이터를 버퍼에 넣는 함수
    // --------------------------------------------------------
    void produce(int item, int producer_id) {
        // condition_variable.wait()는 unique_lock과 함께 사용
        unique_lock<mutex> lock(mtx);

        // 버퍼가 꽉 찼으면 Producer는 기다려야 한다.
        // while을 쓰는 이유:
        // wait에서 깨어난 뒤에도 조건을 다시 확인해야 안전하다.
        while ((int)q.size() == capacity) {
            safe_print("[Producer " + to_string(producer_id) + "] buffer full -> wait");
            not_full.wait(lock);   // 여기서 block 상태로 들어감
            safe_print("[Producer " + to_string(producer_id) + "] wake-up");
        }

        // 버퍼에 자리가 있으므로 데이터 삽입
        q.push(item);

        // 로그 출력
        if (item == POISON_PILL) {
            safe_print("[Main] inserted STOP signal | buffer size = " + to_string(q.size()));
        } else {
            safe_print("[Producer " + to_string(producer_id) +
                       "] produced: " + to_string(item) +
                       " | buffer size = " + to_string(q.size()));
        }

        // 새로운 데이터가 들어왔으므로 Consumer 하나를 깨움
        not_empty.notify_one();
    }

    // --------------------------------------------------------
    // Consumer가 데이터를 버퍼에서 꺼내는 함수
    // --------------------------------------------------------
    int consume(int consumer_id) {
        unique_lock<mutex> lock(mtx);

        // 버퍼가 비어 있으면 Consumer는 기다려야 한다.
        while (q.empty()) {
            safe_print("[Consumer " + to_string(consumer_id) + "] buffer empty -> wait");
            not_empty.wait(lock);   // 여기서 block 상태로 들어감
            safe_print("[Consumer " + to_string(consumer_id) + "] wake-up");
        }

        // 꺼낼 데이터가 있으므로 가져오기
        int item = q.front();
        q.pop();

        // 로그 출력
        if (item == POISON_PILL) {
            safe_print("[Consumer " + to_string(consumer_id) +
                       "] received STOP signal | buffer size = " + to_string(q.size()));
        } else {
            safe_print("[Consumer " + to_string(consumer_id) +
                       "] consumed: " + to_string(item) +
                       " | buffer size = " + to_string(q.size()));
        }

        // 하나 꺼냈으므로 Producer 하나를 깨움
        not_full.notify_one();

        return item;
    }
};

// ============================================================
// Producer 스레드 함수
// 각 Producer는 items_to_produce 개수만큼 데이터 생산
// ============================================================
void producer(Buffer& buffer, int producer_id, int items_to_produce) {
    for (int i = 0; i < items_to_produce; i++) {
        // 로그가 너무 빠르게 지나가지 않게 약간 sleep
        this_thread::sleep_for(chrono::milliseconds(100));

        // 어떤 Producer가 만든 데이터인지 구분하기 쉽게 값 생성
        // 예: Producer 1 -> 100, 101, 102 ...
        //     Producer 2 -> 200, 201, 202 ...
        int item = producer_id * 100 + i;

        buffer.produce(item, producer_id);
    }

    safe_print("[Producer " + to_string(producer_id) + "] finished");
}

// ============================================================
// Consumer 스레드 함수
// STOP 신호를 받기 전까지 계속 소비
// ============================================================
void consumer(Buffer& buffer, int consumer_id) {
    while (true) {
        // 소비 속도를 조금 조절해서 로그 보기 좋게
        this_thread::sleep_for(chrono::milliseconds(150));

        int item = buffer.consume(consumer_id);

        // STOP 신호를 받으면 종료
        if (item == POISON_PILL) {
            break;
        }

        // 실제 처리했다고 가정하는 로그
        safe_print("[Consumer " + to_string(consumer_id) +
                   "] processed item: " + to_string(item));
    }

    safe_print("[Consumer " + to_string(consumer_id) + "] finished");
}

// ============================================================
// main
// 사용법:
// ./pc_cv [buffer_size] [producer_count] [consumer_count] [items_per_producer]
//
// 예:
// ./pc_cv 3 2 2 5
// ============================================================
int main(int argc, char* argv[]) {
    // 인자 개수 검사
    if (argc != 5) {
        cout << "Usage: " << argv[0]
             << " [buffer_size] [producer_count] [consumer_count] [items_per_producer]\n";
        cout << "Example: " << argv[0] << " 3 2 2 5\n";
        return 1;
    }

    // 명령행 인자 읽기
    int buffer_size = atoi(argv[1]);
    int producer_count = atoi(argv[2]);
    int consumer_count = atoi(argv[3]);
    int items_per_producer = atoi(argv[4]);

    // 잘못된 값 방지
    if (buffer_size <= 0 || producer_count <= 0 || consumer_count <= 0 || items_per_producer <= 0) {
        cout << "All arguments must be positive integers.\n";
        return 1;
    }

    // 공유 버퍼 생성
    Buffer buffer(buffer_size);

    // Producer / Consumer 스레드들을 저장할 벡터
    vector<thread> producers;
    vector<thread> consumers;

    // 시작 정보 출력
    safe_print("==================================================");
    safe_print("Producer-Consumer (Condition Variable + Mutex)");
    safe_print("buffer_size        = " + to_string(buffer_size));
    safe_print("producer_count     = " + to_string(producer_count));
    safe_print("consumer_count     = " + to_string(consumer_count));
    safe_print("items_per_producer = " + to_string(items_per_producer));
    safe_print("==================================================");

    // Consumer 먼저 생성
    // 먼저 실행되면 초반에 "buffer empty -> wait" 로그를 볼 수 있다.
    for (int i = 0; i < consumer_count; i++) {
        consumers.emplace_back(consumer, ref(buffer), i + 1);
    }

    // Producer 생성
    for (int i = 0; i < producer_count; i++) {
        producers.emplace_back(producer, ref(buffer), i + 1, items_per_producer);
    }

    // Producer 종료 대기
    for (auto& t : producers) {
        t.join();
    }

    safe_print("[Main] all producers finished");

    // 모든 Producer가 끝났으니
    // Consumer 수만큼 STOP 신호를 넣어 각 Consumer가 정상 종료하도록 함
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