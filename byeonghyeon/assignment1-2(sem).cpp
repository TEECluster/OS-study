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
    queue<int> buffer;              // 생산된 데이터를 저장하는 공유 버퍼
    const unsigned int size;        // 버퍼의 최대 크기

    sem_t full;                     // 현재 버퍼에 들어 있는 아이템 수
    sem_t empty;                    // 현재 버퍼에 남아 있는 빈 자리 수

    pthread_mutex_t buffer_mutex;   // queue 자체를 보호하는 lock
    pthread_mutex_t print_mutex;    // cout 출력이 섞이지 않도록 보호하는 lock

public:
    // 생성자
    BoundedBuffer(unsigned int size) : size(size) {
        // 처음에는 버퍼가 비어 있으므로
        // full = 0 (들어 있는 데이터 개수 0개)
        sem_init(&full, 0, 0);

        // 처음에는 버퍼가 전부 비어 있으므로
        // empty = size (빈 자리 개수 = 버퍼 크기)
        sem_init(&empty, 0, size);

        // queue 보호용 mutex 초기화
        pthread_mutex_init(&buffer_mutex, nullptr);

        // 출력 보호용 mutex 초기화
        pthread_mutex_init(&print_mutex, nullptr);
    }

    // 소멸자
    ~BoundedBuffer() {
        sem_destroy(&full);
        sem_destroy(&empty);

        pthread_mutex_destroy(&buffer_mutex);
        pthread_mutex_destroy(&print_mutex);
    }

    // 생산자 함수
    void produce(int item, int producer_id) {
        // [1] 빈 자리가 있는지 확인
        // empty가 0이면 여기서 자동으로 대기(block)한다.
        // sem_getvalue()로 미리 block 여부를 판단하지 않고,
        // sem_wait()가 check + decrement + block 판단을 한 번에 처리한다.
        sem_wait(&empty);

        int current_size;

        // [2] 실제 공유 버퍼(queue) 작업은 buffer_mutex로 보호
        pthread_mutex_lock(&buffer_mutex);

        buffer.push(item);              // 버퍼에 아이템 삽입
        current_size = buffer.size();   // 현재 버퍼 크기 저장

        pthread_mutex_unlock(&buffer_mutex);

        // [3] 출력은 버퍼 lock이 아니라 출력 전용 lock으로 보호
        // 이렇게 해야 queue 작업과 출력 작업을 분리할 수 있다.
        pthread_mutex_lock(&print_mutex);

        cout << "[Producer " << producer_id << "] Produced: "
             << item << " | Buffer size: " << current_size << endl;

        pthread_mutex_unlock(&print_mutex);

        // [4] 아이템이 1개 늘어났으므로 full 증가
        // 이제 소비자가 꺼낼 수 있는 데이터가 1개 생김
        sem_post(&full);
    }

    // 소비자 함수
    void consume(int consumer_id) {
        // [1] 꺼낼 데이터가 있는지 확인
        // full이 0이면 여기서 자동으로 대기(block)한다.
        sem_wait(&full);

        int item;
        int current_size;

        // [2] 실제 공유 버퍼(queue) 작업은 buffer_mutex로 보호
        pthread_mutex_lock(&buffer_mutex);

        item = buffer.front();          // 맨 앞 데이터 확인
        buffer.pop();                   // 버퍼에서 제거
        current_size = buffer.size();   // 현재 버퍼 크기 저장

        pthread_mutex_unlock(&buffer_mutex);

        // [3] 출력은 출력 전용 lock으로 보호
        pthread_mutex_lock(&print_mutex);

        cout << "[Consumer " << consumer_id << "] Consumed: "
             << item << " | Buffer size: " << current_size << endl;

        pthread_mutex_unlock(&print_mutex);

        // [4] 아이템을 1개 꺼냈으므로 empty 증가
        // 이제 생산자가 사용할 수 있는 빈 자리가 1개 늘어남
        sem_post(&empty);
    }
};

// 생산자 스레드 함수
void producer(BoundedBuffer& buffer, int id) {
    for (int i = 0; i < 5; ++i) {
        // 0~99 사이의 랜덤 값 생성
        int item = rand() % 100;

        // 버퍼에 데이터 생산
        buffer.produce(item, id);

        // 실행 흐름이 한쪽으로만 몰리지 않도록 잠시 대기
        this_thread::sleep_for(chrono::milliseconds(rand() % 1000));
    }
}

// 소비자 스레드 함수
void consumer(BoundedBuffer& buffer, int id) {
    for (int i = 0; i < 5; ++i) {
        // 버퍼에서 데이터 소비
        buffer.consume(id);

        // 실행 흐름이 한쪽으로만 몰리지 않도록 잠시 대기
        this_thread::sleep_for(chrono::milliseconds(rand() % 1000));
    }
}

int main() {
    // 실행할 때마다 다른 난수를 만들기 위해 seed 설정
    srand(time(nullptr));

    const int BUFFER_SIZE = 5;

    // 크기 5인 공유 버퍼 생성
    BoundedBuffer buffer(BUFFER_SIZE);

    // 생산자 2개, 소비자 2개 생성
    thread producers[2];
    thread consumers[2];

    // 스레드 실행
    for (int i = 0; i < 2; ++i) {
        producers[i] = thread(producer, ref(buffer), i);
        consumers[i] = thread(consumer, ref(buffer), i);
    }

    // 모든 스레드가 끝날 때까지 대기
    for (int i = 0; i < 2; ++i) {
        producers[i].join();
        consumers[i].join();
    }

    return 0;
}