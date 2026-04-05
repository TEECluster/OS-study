#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <string>

using namespace std;


// 공유 버퍼의 크기
const int BUFFER_SIZE = 5;

// Producer(생산자) 개수
const int NUM_PRODUCERS = 2;

// Consumer(소비자) 개수
const int NUM_CONSUMERS = 2;

// 각 Producer가 생산할 아이템 개수
const int ITEMS_PER_PRODUCER = 10;

// 공유 자원(Shared Data)
// 고정 크기의 원형 버퍼(circular buffer)
// 실제 큐처럼 사용하지만 vector로 구현
vector<int> buffer(BUFFER_SIZE, 0);

// 다음에 데이터를 삽입할 위치
int in = 0;

// 다음에 데이터를 꺼낼 위치
int out = 0;

// 현재 버퍼 안에 들어있는 데이터 개수
int count = 0;


// 동기화 도구
// buffer, in, out, count를 보호하는 mutex
mutex mtx;

// 버퍼가 가득 찼을 때 Producer가 기다리는 조건변수
condition_variable not_full;

// 버퍼가 비었을 때 Consumer가 기다리는 조건변수
condition_variable not_empty;

// cout 출력이 여러 스레드에서 섞이지 않도록 보호하는 mutex
mutex print_mtx;



// 출력 
// 단순 문자열 한 줄을 안전하게 출력하는 함수
// 여러 스레드가 동시에 호출해도 한 줄 단위로 안 섞이게 해준다
void safePrint(const string& msg) {
    lock_guard<mutex> lock(print_mtx);
    cout << msg << '\n';
}

// 로그 메시지 + 버퍼 상태를 한 번에 출력하는 함수
// 왜 따로 함수로 만들었냐면,
// safePrint(log)와 printBuffer()를 따로 호출하면
// 그 사이에 다른 스레드가 끼어들 수 있기 때문이다.
// 그래서 "하나의 작업 결과"를 한 번에 묶어서 출력한다.
void printState(const string& msg, const vector<int>& snapshot, int snapshot_count) {
    lock_guard<mutex> lock(print_mtx);

    cout << msg << '\n';
    cout << "    [buffer] ";

    // snapshot은 이미 버퍼 락(mtx)을 잡고 복사해둔 안전한 데이터
    // 따라서 여기서는 mtx 없이 출력만 하면 된다
    for (int v : snapshot) {
        cout << v << " ";
    }

    cout << "| count = " << snapshot_count << '\n';
}

// Producer 함수
// id: Producer 번호
void producer(int id) {
    // 각 Producer는 정해진 개수만큼 아이템 생산
    for (int i = 0; i < ITEMS_PER_PRODUCER; i++) {
        // 예: Producer 0 -> 0,1,2,3...
        //     Producer 1 -> 100,101,102...
        // 서로 다른 Producer가 만든 값임을 구분하기 쉽도록 생성
        int item = id * 100 + i;

        // 출력용으로 사용할 정보들
        // 공유 데이터는 락 밖에서 직접 읽으면 안 되므로,
        // 임계구역 안에서 snapshot을 복사해두고
        // 락을 푼 뒤 출력에 사용한다.
        vector<int> snapshot;
        int snapshot_count;
        string log_msg;

        // 공유 버퍼 접근을 위한 락
        unique_lock<mutex> lock(mtx);

        // 버퍼가 가득 차 있다면 생산 불가
        // 이때 바로 wait만 해도 되지만,
        // "지금 block 상태가 되었다"는 로그를 먼저 남기기 위해
        // if로 한 번 체크한다.
        if (count == BUFFER_SIZE) {
            // 출력은 print_mtx로 보호해야 하므로
            // mtx를 잠시 풀고 출력
            lock.unlock();
            safePrint("[Producer " + to_string(id) + "] buffer full -> block");
            lock.lock();
        }

        // 조건이 만족될 때까지 대기
        // wait(lock, predicate)는 내부적으로:
        // 1) lock을 자동으로 풀고 잠듦
        // 2) 깨면 다시 lock을 잡음
        // 3) predicate를 다시 검사함
        // 따라서 busy waiting 없이 안전하게 대기 가능
        not_full.wait(lock, [] { return count < BUFFER_SIZE; });

        // ===== 임계구역 시작 =====
        // 이제 버퍼에 빈 자리가 있으므로 생산 가능

        // 현재 삽입 위치를 따로 저장 (로그 출력용)
        int insert_index = in;

        // 버퍼에 데이터 삽입
        buffer[in] = item;

        // 원형 버퍼이므로 다음 위치는 modulo 연산으로 순환
        in = (in + 1) % BUFFER_SIZE;

        // 버퍼 안 데이터 개수 증가
        count++;

        // 출력용 snapshot 복사
        // 중요한 점:
        // 실제 buffer를 락 밖에서 직접 출력하지 않고,
        // 락 안에서 안전하게 복사만 해둔다.
        snapshot = buffer;
        snapshot_count = count;

        // 로그 메시지 생성
        log_msg = "[Producer " + to_string(id) + "] produced " +
                  to_string(item) + " at index " + to_string(insert_index);

        // ===== 임계구역 끝 =====

        // 버퍼 락 해제
        lock.unlock();

        // 로그 출력은 별도의 출력 락으로 보호
        printState(log_msg, snapshot, snapshot_count);

        // 버퍼에 아이템이 생겼으니 대기 중인 Consumer 하나 깨움
        not_empty.notify_one();

        // 실행 흐름을 보기 쉽게 일부러 sleep
        // 실제 동기화에는 필수는 아니고, 실험/로그 관찰용
        this_thread::sleep_for(chrono::milliseconds(100));
    }
}


// Consumer 함수
// id: Consumer 번호
// items_to_consume: 이 Consumer가 총 몇 개를 소비할지
void consumer(int id, int items_to_consume) {
    for (int i = 0; i < items_to_consume; i++) {
        // 출력용 정보 저장 변수
        vector<int> snapshot;
        int snapshot_count;
        string log_msg;

        // 공유 버퍼 접근용 락
        unique_lock<mutex> lock(mtx);

        // 버퍼가 비어 있으면 소비 불가
        if (count == 0) {
            // 출력은 버퍼 락과 분리해서 처리
            lock.unlock();
            safePrint("[Consumer " + to_string(id) + "] buffer empty -> block");
            lock.lock();
        }

        // 버퍼에 데이터가 들어올 때까지 대기
        not_empty.wait(lock, [] { return count > 0; });

        // ===== 임계구역 시작 =====
        // 이제 소비 가능

        // 현재 제거 위치 저장 (로그용)
        int remove_index = out;

        // 버퍼에서 아이템 꺼내기
        int item = buffer[out];

        // 보기 쉽게 소비한 자리를 0으로 초기화
        // 원형 버퍼 구현에서 필수는 아니지만,
        // 로그로 상태를 볼 때 직관적이라서 넣은 것
        buffer[out] = 0;

        // 다음 제거 위치로 이동
        out = (out + 1) % BUFFER_SIZE;

        // 버퍼 안 데이터 개수 감소
        count--;

        // 출력용 snapshot 복사
        snapshot = buffer;
        snapshot_count = count;

        // 로그 메시지 생성
        log_msg = "[Consumer " + to_string(id) + "] consumed " +
                  to_string(item) + " at index " + to_string(remove_index);

        // ===== 임계구역 끝 =====

        // 버퍼 락 해제
        lock.unlock();

        // 출력은 출력용 락으로만 보호
        printState(log_msg, snapshot, snapshot_count);

        // 버퍼에 빈자리가 생겼으니 대기 중인 Producer 하나 깨움
        not_full.notify_one();

        // 실험용 지연
        this_thread::sleep_for(chrono::milliseconds(150));
    }
}



// main 함수
int main() {
    // Producer 스레드들을 저장할 벡터
    vector<thread> producers;

    // Consumer 스레드들을 저장할 벡터
    vector<thread> consumers;

    // 전체 생산량 = 생산자 수 × 각 생산량
    int total_items = NUM_PRODUCERS * ITEMS_PER_PRODUCER;

    // Consumer들에게 소비량을 최대한 균등하게 분배
    int base_items = total_items / NUM_CONSUMERS;
    int extra = total_items % NUM_CONSUMERS;

    // ==========================
    // Producer 스레드 생성
    // ==========================
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        producers.emplace_back(producer, i);
    }

    // Consumer 스레드 생성
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        // total_items가 NUM_CONSUMERS로 나누어떨어지지 않을 수도 있으므로
        // 앞쪽 Consumer에게 1개씩 extra를 더 배정
        int items_to_consume = base_items + (i < extra ? 1 : 0);
        consumers.emplace_back(consumer, i, items_to_consume);
    }

    // ==========================
    // 모든 Producer 종료 대기
    // ==========================
    for (auto& t : producers) {
        t.join();
    }

    // 모든 Consumer 종료 대기
    for (auto& t : consumers) {
        t.join();
    }

    // 프로그램 종료 메시지
    safePrint("[main] finished successfully");

    return 0;
}