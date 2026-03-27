#include <iostream>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <chrono>
using namespace std;

// === 공유 자원 및 동기화 도구 설정 ===
const int BUFFER_SIZE = 5;          // 버퍼 최대 크기
queue<int> buffer;                  // 공유 버퍼

mutex mtx_buffer;                   // 1. 컨디션 보호용 락 (버퍼 접근 보호)
mutex mtx_print;                    // 2. 출력용 락 (콘솔 로그 꼬임 방지)

condition_variable cv_producer;     // 생산자용 대기실 및 신호기
condition_variable cv_consumer;     // 소비자용 대기실 및 신호기

// === 생산자(Producer) 스레드 동작 ===
void producer(int id, int items_to_produce) {
    for (int i = 1; i <= items_to_produce; ++i) {
        int data = (id * 100) + i; 
        int current_size = 0; // 락을 풀고 출력하기 위해 버퍼 크기를 임시 저장

        // 1. [컨디션 보호용 락] 버퍼 조작을 위해 문을 잠근다.
        unique_lock<mutex> lock(mtx_buffer);

        // (유지된 while문) 버퍼가 꽉 찼는지 확인
        while (buffer.size() == BUFFER_SIZE) {
            {   // [출력용 락] 화면에 글자를 찍을 때만 잠시 출력용 락을 쓴다.
                lock_guard<mutex> print_lock(mtx_print);
                cout << "[생산자 " << id << "]  버퍼 가득 참 (Block) -> 대기실로 이동...\n";
            }
            
            // 컨디션 락(mtx_buffer)을 잠시 풀고 잠든다.
            cv_producer.wait(lock); 
            
            {   // [출력용 락]
                lock_guard<mutex> print_lock(mtx_print);
                cout << "[생산자 " << id << "]  빈 자리 생김 (Wake-up) -> 다시 작업 시작!\n";
            }
        }

        // 2. 버퍼에 데이터를 넣는다
        buffer.push(data);
        current_size = buffer.size(); // 락이 걸려있을 때 안전하게 크기만 복사해 둡니다.

        // 3. 버퍼 작업이 끝났으니 컨디션 보호용 락을 푼다
        lock.unlock();

        // 4. 대기 중인 소비자 중 한 개를 깨운다.
        cv_consumer.notify_one();

        // 5. [출력용 락] 창고 문 밖으로 나와서, 로그를 출력한다.
        {
            lock_guard<mutex> print_lock(mtx_print);
            cout << "[생산자 " << id << "]  데이터 생산: " << data << " (현재 버퍼 크기: " << current_size << ")\n";
        }

        this_thread::sleep_for(chrono::milliseconds(50));
    }
}


// === 소비자(Consumer) 스레드 동작 ===
void consumer(int id, int items_to_consume) {
    for (int i = 1; i <= items_to_consume; ++i) {
        
        int data = 0;
        int current_size = 0;

        // 1. [컨디션 보호용 락] 버퍼 조작을 위해 문을 잠근다.
        unique_lock<mutex> lock(mtx_buffer);

        // (유지된 while문) 버퍼가 비어 있는지 확인
        while (buffer.empty()) {
            {   // [출력용 락]
                lock_guard<mutex> print_lock(mtx_print);
                cout << "[소비자 " << id << "]  버퍼 비었음 (Block) -> 대기실로 이동...\n";
            }
            
            cv_consumer.wait(lock);
            
            {   // [출력용 락]
                lock_guard<mutex> print_lock(mtx_print);
                cout << "[소비자 " << id << "]  데이터 들어옴 (Wake-up) -> 다시 작업 시작!\n";
            }
        }

        // 2. 버퍼에서 데이터를 꺼낸다
        data = buffer.front();
        buffer.pop();
        current_size = buffer.size(); // 크기 복사

        // 3. 버퍼 작업이 끝났으니 컨디션 보호용 락을 즉시 푼다
        lock.unlock();

        // 4. 대기 중인 생산자 중 한 명을 깨운다
        cv_producer.notify_one();

        // 5. [출력용 락] 컨디션 락 밖에서  로그를 출력.
        {
            lock_guard<mutex> print_lock(mtx_print);
            cout << "[소비자 " << id << "]  데이터 소비: " << data << " (현재 버퍼 크기: " << current_size << ")\n";
        }
        
        this_thread::sleep_for(chrono::milliseconds(100));
    }
}

int main() {
    cout << "=== Bounded Buffer 동기화 시스템 시작 (CV + Mutex) ===\n\n";

    int num_producers = 2;
    int num_consumers = 2;
    int items_per_thread = 5;

    vector<thread> threads;

    for (int i = 1; i <= num_producers; ++i) {
        threads.push_back(thread(producer, i, items_per_thread));
    }
    for (int i = 1; i <= num_consumers; ++i) {
        threads.push_back(thread(consumer, i, items_per_thread));
    }

    for (auto& t : threads) {
        t.join();
    }

    cout << "\n=== 모든 작업이 안전하게 완료되었습니다. ===\n";
    return 0;
}
