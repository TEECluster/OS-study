#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <cstdlib>
#include <chrono>
#include <ctime>

using namespace std;

class BoundedBuffer {
private:
    queue<int> buffer;              // 공유 버퍼
    const unsigned int size;        // 버퍼 최대 크기

    mutex buffer_mutex;             // 버퍼(queue) 보호용 mutex
    mutex print_mutex;              // 출력 전용 mutex

    condition_variable not_full;    // 버퍼가 꽉 차지 않았음을 알리는 조건변수
    condition_variable not_empty;   // 버퍼가 비어 있지 않음을 알리는 조건변수

public:
    // 생성자
    BoundedBuffer(unsigned int size) : size(size) {}

    // 생산자 함수
    void produce(int item, int producer_id) {
        int current_size;

        // unique_lock은 condition_variable.wait()와 함께 사용하기 위해 필요함
        unique_lock<mutex> lock(buffer_mutex);

        // 버퍼가 가득 차 있으면 여기서 대기
        // "미리 block될지 판단"하지 않고,
        // wait()가 조건 확인 + 대기 + 깨어난 뒤 재확인을 처리함
        not_full.wait(lock, [this]() {
            return buffer.size() < size;
        });

        // 조건이 만족되면 버퍼에 데이터 삽입
        buffer.push(item);
        current_size = buffer.size();

        // 버퍼 작업 끝났으므로 lock 해제
        lock.unlock();

        // 소비자에게 "버퍼가 비어있지 않다"는 신호 전달
        not_empty.notify_one();

        // 출력은 출력 전용 mutex로 따로 보호
        lock_guard<mutex> print_lock(print_mutex);
        cout << "[Producer " << producer_id << "] Produced: "
             << item << " | Buffer size: " << current_size << endl;
    }

    // 소비자 함수
    void consume(int consumer_id) {
        int item;
        int current_size;

        unique_lock<mutex> lock(buffer_mutex);

        // 버퍼가 비어 있으면 여기서 대기
        // wait()가 조건 확인과 block 판단을 직접 처리함
        not_empty.wait(lock, [this]() {
            return !buffer.empty();
        });

        // 조건이 만족되면 버퍼에서 데이터 꺼냄
        item = buffer.front();
        buffer.pop();
        current_size = buffer.size();

        // 버퍼 작업 끝났으므로 lock 해제
        lock.unlock();

        // 생산자에게 "버퍼에 빈 자리가 생겼다"는 신호 전달
        not_full.notify_one();

        // 출력은 출력 전용 mutex로 따로 보호
        lock_guard<mutex> print_lock(print_mutex);
        cout << "[Consumer " << consumer_id << "] Consumed: "
             << item << " | Buffer size: " << current_size << endl;
    }
};

// 생산자 스레드 함수
void producer(BoundedBuffer& buffer, int id) {
    for (int i = 0; i < 5; ++i) {
        int item = rand() % 100;  // 0~99 랜덤 값 생성
        buffer.produce(item, id);
        this_thread::sleep_for(chrono::milliseconds(rand() % 1000));
    }
}

// 소비자 스레드 함수
void consumer(BoundedBuffer& buffer, int id) {
    for (int i = 0; i < 5; ++i) {
        buffer.consume(id);
        this_thread::sleep_for(chrono::milliseconds(rand() % 1000));
    }
}

int main() {
    srand(time(nullptr));   // 실행할 때마다 다른 난수 생성

    const int BUFFER_SIZE = 5;
    BoundedBuffer buffer(BUFFER_SIZE);

    thread producers[2];
    thread consumers[2];

    // 생산자 2개, 소비자 2개 실행
    for (int i = 0; i < 2; ++i) {
        producers[i] = thread(producer, ref(buffer), i);
        consumers[i] = thread(consumer, ref(buffer), i);
    }

    // 모든 스레드 종료까지 대기
    for (int i = 0; i < 2; ++i) {
        producers[i].join();
        consumers[i].join();
    }

    return 0;
}