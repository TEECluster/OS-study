#include <iostream>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <chrono>

using namespace std; // 

// 출력용 락은 여러 큐에서 공유할 수도 있으므로 외부에 둔다.
mutex mtx_print;

// 공유 자원과 동기화 객체들을 클래스 하나로 완벽히 캡슐화
template <typename T>
class ThreadSafeBoundedQueue {
private: //데이터는 숨기고, 안전한 검증 로직이 포함된 함수로만 접근하게 만드는 과정으로
         // 외부에서 조작할 가능성을 차단하는 과정이다. 
    queue<T> buffer;
    mutex mtx_buffer;
    condition_variable cv_producer;
    condition_variable cv_consumer;
    const size_t max_size;

public:
    explicit ThreadSafeBoundedQueue(size_t size) : max_size(size) {}

    void push(T data, int producer_id) {
        unique_lock<mutex> lock(mtx_buffer);

        // while문 대신 람다(Lambda)식을 사용하여 코드 간소화(wait안에 조건 검사 로직을 내재함)
        cv_producer.wait(lock, [this]() { return buffer.size() < max_size; });

        buffer.push(data);
        size_t current_size = buffer.size();

        // 락을 먼저 풀고 Notify (기존의 훌륭한 최적화)
        lock.unlock();
        cv_consumer.notify_one();

        // 콘솔 출력
        lock_guard<mutex> print_lock(mtx_print);
        cout << "[생산자 " << producer_id << "] 데이터 생산: " << data 
             << " (현재 버퍼 크기: " << current_size << ")\n";
    }

    T pop(int consumer_id) {
        unique_lock<mutex> lock(mtx_buffer);

        // 람다식 사용 (버퍼가 비어있지 않을 때까지 대기)
        cv_consumer.wait(lock, [this]() { return !buffer.empty(); });

        T data = buffer.front();
        buffer.pop();
        size_t current_size = buffer.size();

        // 락을 먼저 풀고 Notify
        lock.unlock();
        cv_producer.notify_one();

        // 콘솔 출력
        lock_guard<mutex> print_lock(mtx_print);
        cout << "[소비자 " << consumer_id << "] 데이터 소비: " << data 
             << " (현재 버퍼 크기: " << current_size << ")\n";

        return data;
    }
};

// 최대 크기가 5인 큐 인스턴스 생성
ThreadSafeBoundedQueue<int> safe_queue(5);

// === 생산자(Producer) 스레드 동작 ===
void producer(int id, int items_to_produce) {
    for (int i = 1; i <= items_to_produce; ++i) {
        int data = (id * 100) + i;
        safe_queue.push(data, id); 
        this_thread::sleep_for(chrono::milliseconds(50));
    }
}

// === 소비자(Consumer) 스레드 동작 ===
void consumer(int id, int items_to_consume) {
    for (int i = 1; i <= items_to_consume; ++i) {
        safe_queue.pop(id); 
        this_thread::sleep_for(chrono::milliseconds(100));
    }
}

int main() {
    cout << "=== Bounded Buffer 동기화 시스템 (클래스 캡슐화 + jthread) ===\n\n";

    int num_producers = 2;
    int num_consumers = 2;
    int items_per_thread = 5;

    // 인위적인 스코프(블록) 생성
    {
        // C++20 jthread 사용하면 객체가 소멸될 때 자동으로 join()을 호출
        vector<jthread> threads;

        for (int i = 1; i <= num_producers; ++i) {
            threads.emplace_back(producer, i, items_per_thread);
        }
        for (int i = 1; i <= num_consumers; ++i) {
            threads.emplace_back(consumer, i, items_per_thread);
        }
        
        // 이 블록(스코프)이 끝나는 "}" 지점에서 threads 벡터가 소멸되며 
        // 내부의 jthread 소멸자가 자동으로 join()을 호출한다.
    }

    cout << "\n=== 모든 작업이 안전하게 완료되었습니다. ===\n";
    return 0;
}