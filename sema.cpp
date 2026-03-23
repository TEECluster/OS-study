#include <iostream>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable> // 컨디션 변수 헤더파일은 필수
#include <vector>
#include <chrono>
using namespace std;


// === 공유 자원 및 동기화 도구 설정 ===
const int BUFFER_SIZE = 5;          // 버퍼 최대 크기
queue<int> buffer;                  // 공유 버퍼

mutex mtx;                          // 버퍼 접근을 보호하는 자물쇠 (경비원)
condition_variable cv_producer;     // 생산자용 대기실 및 신호기
condition_variable cv_consumer;     // 소비자용 대기실 및 신호기


// === 생산자(Producer) 스레드 동작 ===
void producer(int id, int items_to_produce) {
    for (int i = 1; i <= items_to_produce; ++i) {
        int data = (id * 100) + i; // 데이터 생성 (누가 어떤 데이터를 만들었는지 알려주는 과정)
        //예를 들어 1번 쓰레드가 1번 데이터를 이용하면 101, 2번쓰레드가 1번을 이용하면 201로 해 
        //몇번 쓰레드가 만든 데이터인지 구분할 수 있게 한다. 

        // 1. 자물쇠를 획득하고 임계 구역에 들어간다 (자동 잠금)
        unique_lock<mutex> lock(mtx);

        // 2. 버퍼가 꽉 찼는지 확인
        while (buffer.size() == BUFFER_SIZE) {
            cout << "[생산자 " << id << "]  버퍼 가득 참 (Block) -> 대기실로 이동...\n";
            
            // 자물쇠를 잠시 풀고 잠듬 (누군가 깨워주면 다시 자물쇠를 잠그고 일어남)
            cv_producer.wait(lock); 
            // 소비자가 깨우기 전까지는 실행되지 않음
            cout << "[생산자 " << id << "]  빈 자리 생김 (Wake-up) -> 다시 작업 시작!\n";
        }

        // 3. 버퍼에 데이터를 넣는다. (임계 구역 안에서 안전하게)
        buffer.push(data);
        cout << "[생산자 " << id << "]  데이터 생산: " << data << " (현재 버퍼 크기: " << buffer.size() << ")\n";

        // 4. 작업을 마쳤으니 자물쇠를 먼저 푼다 (소비자가 바로 들어올 수 있게 배려)
        lock.unlock();

        // 5. 데이터 들어왔음을 확인하고 대기 중인 소비자 중 한 명을 깨운다.
        cv_consumer.notify_one();

        // 시간 지연용, 확인용 명령어
        this_thread::sleep_for(chrono::milliseconds(50));
    }
}


// === 소비자(Consumer) 스레드 동작 ===
void consumer(int id, int items_to_consume) {
    for (int i = 1; i <= items_to_consume; ++i) {
        
        // 1. 자물쇠를 획득하고 임계 구역에 들어간다
        unique_lock<mutex> lock(mtx);

        // 2. 버퍼가 비어 있는지 확인
        while (buffer.empty()) {
            cout << "[소비자 " << id << "]  버퍼 비었음 (Block) -> 대기실로 이동...\n";
            
            // 자물쇠를 잠시 풀고 잠듬
            cv_consumer.wait(lock);
            
            cout << "[소비자 " << id << "]  데이터 들어옴 (Wake-up) -> 다시 작업 시작!\n";
        }

        // 3. 버퍼에서 데이터를 꺼낸다
        int data = buffer.front();
        buffer.pop();
        cout << "[소비자 " << id << "]  데이터 소비: " << data << " (현재 버퍼 크기: " << buffer.size() << ")\n";

        // 4. 자물쇠를 먼저 풉니다.
        lock.unlock();

        // 5. 빈 자리 하나 생겼음을 확인하고 대기 중인 생산자 중 한 명을 깨웁니다.
        cv_producer.notify_one();

        
        this_thread::sleep_for(chrono::milliseconds(100));
    }
}


int main() {
    cout << "=== Bounded Buffer 동기화 시스템 시작 (CV + Mutex) ===\n\n";

    int num_producers = 2;
    int num_consumers = 2;
    int items_per_thread = 5; // 각 스레드가 처리할 데이터 개수

    vector<thread> threads;

    // 생산자 스레드 2개 생성
    for (int i = 1; i <= num_producers; ++i) {
        threads.push_back(thread(producer, i, items_per_thread));
    }

    // 소비자 스레드 2개 생성
    for (int i = 1; i <= num_consumers; ++i) {
        threads.push_back(thread(consumer, i, items_per_thread));
    }

    // 모든 스레드가 끝날 때까지 대기 (Join)
    for (auto& t : threads) { //auto를 이용해 자동으로 쓰레드들의 완료를 확인
        t.join();
    }

    cout << "\n=== 모든 작업이 안전하게 완료되었습니다. ===\n";
    return 0;
}