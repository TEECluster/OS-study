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
    queue<int> buffer;               // 공유 버퍼
    const unsigned int size;         // 버퍼 최대 크기
    mutex mtx;                       // 버퍼 보호용 mutex
    condition_variable not_full;     // 버퍼에 빈 자리가 생길 때까지 기다리는 조건 변수
    condition_variable not_empty;    // 버퍼에 데이터가 생길 때까지 기다리는 조건 변수

public:
    BoundedBuffer(unsigned int size) : size(size) {}

    void produce(int item, int producer_id) {
        int current_size;

        unique_lock<mutex> lock(mtx);

        // 버퍼가 가득 차 있으면 빈 자리가 생길 때까지 대기
        not_full.wait(lock, [this]() { return buffer.size() < size; });

        buffer.push(item);
        current_size = buffer.size();

        lock.unlock();

        // 출력은 임계구역 밖에서 수행
        cout << "[Producer " << producer_id << "] Produced: "
             << item << " | Buffer size: " << current_size << endl;

        // 버퍼가 비어 있던 소비자에게 알림
        not_empty.notify_one();
    }

    void consume(int consumer_id) {
        int item;
        int current_size;

        unique_lock<mutex> lock(mtx);

        // 버퍼가 비어 있으면 데이터가 들어올 때까지 대기
        not_empty.wait(lock, [this]() { return !buffer.empty(); });

        item = buffer.front();
        buffer.pop();
        current_size = buffer.size();

        lock.unlock();

        // 출력은 임계구역 밖에서 수행
        cout << "[Consumer " << consumer_id << "] Consumed: "
             << item << " | Buffer size: " << current_size << endl;

        // 버퍼가 가득 차서 기다리던 생산자에게 알림
        not_full.notify_one();
    }
};

// 생산자 스레드 함수
void producer(BoundedBuffer& buffer, int id) {
    for (int i = 0; i < 5; ++i) {
        int item = rand() % 100;
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
    srand(time(nullptr));

    const int BUFFER_SIZE = 5;
    BoundedBuffer buffer(BUFFER_SIZE);

    thread producers[2];
    thread consumers[2];

    for (int i = 0; i < 2; ++i) {
        producers[i] = thread(producer, ref(buffer), i);
        consumers[i] = thread(consumer, ref(buffer), i);
    }

    for (int i = 0; i < 2; ++i) {
        producers[i].join();
        consumers[i].join();
    }

    return 0;
}