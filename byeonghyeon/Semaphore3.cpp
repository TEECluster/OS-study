#include <iostream>
#include <thread>
#include <semaphore.h>
#include <queue>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <pthread.h>

using namespace std;

class BoundedBuffer {
private:
    queue<int> buffer;          // 공유 버퍼
    const unsigned int size;    // 버퍼 최대 크기
    sem_t full;                 // 버퍼에 들어 있는 아이템 수
    sem_t empty;                // 버퍼에 남아 있는 빈 자리 수
    pthread_mutex_t mutex;      // queue 보호용 mutex

public:
    // 생성자
    BoundedBuffer(unsigned int size) : size(size) {
        sem_init(&full, 0, 0);          // 처음엔 버퍼가 비어 있으므로 0
        sem_init(&empty, 0, size);      // 처음엔 빈 자리가 size개
        pthread_mutex_init(&mutex, nullptr);
    }

    // 소멸자
    ~BoundedBuffer() {
        sem_destroy(&full);
        sem_destroy(&empty);
        pthread_mutex_destroy(&mutex);
    }

    // 생산자 메서드
    void produce(int item, int producer_id) {
        // 빈 자리가 없으면 여기서 대기
        sem_wait(&empty);

        int current_size;

        // 실제 버퍼 작업만 임계구역 안에서 수행
        pthread_mutex_lock(&mutex);
        buffer.push(item);
        current_size = buffer.size();
        pthread_mutex_unlock(&mutex);

        // 출력은 임계구역 밖에서 수행
        cout << "[Producer " << producer_id << "] Produced: "
             << item << " | Buffer size: " << current_size << endl;

        // 소비자가 사용할 수 있는 아이템 수 1 증가
        sem_post(&full);
    }

    // 소비자 메서드
    void consume(int consumer_id) {
        // 꺼낼 아이템이 없으면 여기서 대기
        sem_wait(&full);

        int item;
        int current_size;

        // 실제 버퍼 작업만 임계구역 안에서 수행
        pthread_mutex_lock(&mutex);
        item = buffer.front();
        buffer.pop();
        current_size = buffer.size();
        pthread_mutex_unlock(&mutex);

        // 출력은 임계구역 밖에서 수행
        cout << "[Consumer " << consumer_id << "] Consumed: "
             << item << " | Buffer size: " << current_size << endl;

        // 생산자가 사용할 수 있는 빈 자리 수 1 증가
        sem_post(&empty);
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