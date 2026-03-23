#include <iostream>
#include <thread>
#include <vector>
#include <deque>
#include <string>
#include <mutex>
#include <condition_variable>
#include <semaphore>
#include <chrono>

using namespace std;

// Consumer를 종료 FLAG
constexpr int POISON = -1;

// 현재 어떤 동기화 방식으로 실행할지 구분하는 용도
// CV  : condition_variable + mutex 방식
// SEM : semaphore 방식
enum Mode {
    CV,
    SEM
};

// 하나의 실행 시나리오(실험 설정)를 저장하는 구조체
struct Config {
    string name;       // 시나리오 이름
    Mode mode;         // 동기화 방식
    int buffer_size;   // 공유 버퍼 크기
    int producers;     // Producer 개수
    int consumers;     // Consumer 개수
    int items;         // Producer 1개당 생산할 아이템 수
};

// 실행 결과를 요약하기 위한 통계 구조체.
struct Stats {
    int produced = 0;  // 전체 생산 개수
    int consumed = 0;  // 전체 소비 개수
    int p_block = 0;   // Producer가 block된 횟수
    int c_block = 0;   // Consumer가 block된 횟수
};

// 로그 출력용 구조체
// 여러 스레드가 동시에 cout을 사용하면 출력이 섞이니, mutex로 보호
struct Logger {
    mutex mtx;

    void log(string msg) {
        lock_guard<mutex> lock(mtx);
        cout << msg << endl;
    }
};

// 전역 logger 1개를 사용
Logger logger;

// Condition Variable 방식의 bounded buffer
struct CvBuffer {
    int capacity;                  // 버퍼 최대 크기
    deque<int> q;                  // 실제 데이터를 담는 큐
    mutex mtx;                     // 큐 보호용 mutex
    condition_variable not_full;   // 버퍼가 가득 차지 않았음을 알리는 조건변수
    condition_variable not_empty;  // 버퍼가 비어 있지 않음을 알리는 조건변수

    // Producer가 데이터를 버퍼에 넣는 함수
    void push(int item, int id, Stats& s) {
        unique_lock<mutex> lock(mtx);

        // 버퍼가 꽉 찬 경우 Producer는 기다림
        while (q.size() == capacity) {
            s.p_block++;
            logger.log("[P" + to_string(id) + "] buffer full -> block");
            not_full.wait(lock);    // 대기하는 동안 lock을 반납하고, 깨어나면 다시 lock 획득
            logger.log("[P" + to_string(id) + "] wake-up");
        }

        // 버퍼에 데이터 삽입
        q.push_back(item);
        print("push");

        // 버퍼에 데이터가 생겼으므로 Consumer 하나를 깨우기
        lock.unlock();
        not_empty.notify_one();
    }

    // Consumer가 데이터를 버퍼에서 꺼내는 함수
    int pop(int id, Stats& s) {
        unique_lock<mutex> lock(mtx);

        // 버퍼가 비어 있으면 Consumer는 기다리기
        while (q.empty()) {
            s.c_block++;
            logger.log("[C" + to_string(id) + "] buffer empty -> block");
            not_empty.wait(lock);   // 대기 중에는 lock 반납
            logger.log("[C" + to_string(id) + "] wake-up");
        }

        // 버퍼에서 데이터 하나 꺼내기
        int item = q.front();
        q.pop_front();
        print("pop");

        // 버퍼에 빈 칸이 생겼으므로 Producer 하나를 깨우기
        lock.unlock();
        not_full.notify_one();

        return item;
    }

    // 현재 버퍼 상태를 출력하는 함수
    void print(string action) {
        string msg = "    [CV " + action + "] size=" + to_string(q.size()) + " | ";
        for (int v : q) {
            msg += to_string(v) + " ";
        }
        logger.log(msg);
    }
};

// Semaphore 방식의 bounded buffer
struct SemBuffer {
    int capacity;                  // 버퍼 최대 크기
    deque<int> q;                  // 실제 데이터를 담는 큐
    mutex mtx;                     // 큐 보호용 mutex

    // empty : 현재 비어 있는 칸의 개수
    // full  : 현재 데이터가 들어 있는 칸의 개수
    counting_semaphore<> empty;
    counting_semaphore<> full;

    // 생성자에서 semaphore 초기값 설정
    // empty = capacity (처음엔 모든 칸이 비어 있음)
    // full  = 0        (처음엔 데이터가 없음)
    SemBuffer(int cap) : capacity(cap), empty(cap), full(0) {}

    // Producer가 데이터를 버퍼에 넣는 함수
    void push(int item, int id, Stats& s) {
        // 빈 칸이 없으면 Producer는 block
        if (!empty.try_acquire()) {
            s.p_block++;
            logger.log("[P" + to_string(id) + "] buffer full -> block");
            empty.acquire();    // 빈 칸이 생길 때까지 대기
            logger.log("[P" + to_string(id) + "] wake-up");
        }

        // 실제 큐 수정은 mutex로 보호
        {
            lock_guard<mutex> lock(mtx);
            q.push_back(item);
            print("push");
        }

        // 데이터가 하나 늘었으므로 full 증가
        full.release();
    }

    // Consumer가 데이터를 버퍼에서 꺼내는 함수
    int pop(int id, Stats& s) {
        // 꺼낼 데이터가 없으면 Consumer는 block
        if (!full.try_acquire()) {
            s.c_block++;
            logger.log("[C" + to_string(id) + "] buffer empty -> block");
            full.acquire();  // 데이터가 생길 때까지 대기
            logger.log("[C" + to_string(id) + "] wake-up");
        }

        int item;
        {
            lock_guard<mutex> lock(mtx);
            item = q.front();
            q.pop_front();
            print("pop");
        }

        // 빈 칸이 하나 늘었으므로 empty 증가
        empty.release();
        return item;
    }

    // 현재 버퍼 상태 출력
    void print(string action) {
        string msg = "    [SEM " + action + "] size=" + to_string(q.size()) + " | ";
        for (int v : q) {
            msg += to_string(v) + " ";
        }
        logger.log(msg);
    }
};

// 하나의 시나리오를 실행하는 함수
void run(Config cfg) {
    cout << "\n====================================\n";
    cout << "Scenario      : " << cfg.name << "\n";
    cout << "Mode          : " << (cfg.mode == CV ? "Condition Variable" : "Semaphore") << "\n";
    cout << "Buffer Size   : " << cfg.buffer_size << "\n";
    cout << "Producers     : " << cfg.producers << "\n";
    cout << "Consumers     : " << cfg.consumers << "\n";
    cout << "Items/Producer: " << cfg.items << "\n";
    cout << "====================================\n";

    Stats stats;

    // 두 버퍼를 모두 만들되, 실제로는 mode에 따라 하나만 사용
    CvBuffer cv{cfg.buffer_size};
    SemBuffer sem(cfg.buffer_size);

    vector<thread> ps;
    vector<thread> cs;

    auto start = chrono::steady_clock::now();

    // Producer 스레드 생성
    for (int i = 0; i < cfg.producers; i++) {
        ps.emplace_back([&, i]() {
            for (int j = 0; j < cfg.items; j++) {
                // Producer마다 구분 가능한 데이터 생성
                int item = i * 100 + j;

                logger.log("[P" + to_string(i) + "] produced " + to_string(item));

                // 현재 mode에 맞는 방식으로 push 수행
                if (cfg.mode == CV) {
                    cv.push(item, i, stats);
                } else {
                    sem.push(item, i, stats);
                }

                stats.produced++;

                // 로그가 너무 빨리 지나가지 않도록 약간 sleep
                this_thread::sleep_for(chrono::milliseconds(200));
            }

            logger.log("[P" + to_string(i) + "] finished");
        });
    }

    // Consumer 스레드 생성
    for (int i = 0; i < cfg.consumers; i++) {
        cs.emplace_back([&, i]() {
            while (true) {
                int item;

                // 현재 mode에 맞는 방식으로 pop 수행
                if (cfg.mode == CV) {
                    item = cv.pop(i, stats);
                } else {
                    item = sem.pop(i, stats);
                }

                // POISON을 받으면 더 이상 처리할 데이터가 없다는 뜻이므로 종료
                if (item == POISON) {
                    logger.log("[C" + to_string(i) + "] received poison -> exit");
                    break;
                }

                logger.log("[C" + to_string(i) + "] consumed " + to_string(item));
                stats.consumed++;

                this_thread::sleep_for(chrono::milliseconds(300));
            }
        });
    }

    // 모든 Producer 종료 대기
    for (auto& t : ps) {
        t.join();
    }

    logger.log("[Main] all producers finished");

  
    // Consumer 종료를 위해 Consumer 수만큼 POISON을 버퍼에 넣기
    // Consumer 한 명당 하나씩 있어야 모든 Consumer가 종료 가능
    for (int i = 0; i < cfg.consumers; i++) {
        if (cfg.mode == CV) {
            cv.push(POISON, -1, stats);
        } else {
            sem.push(POISON, -1, stats);
        }
    }

    // 모든 Consumer 종료 대기
    for (auto& t : cs) {
        t.join();
    }

    auto end = chrono::steady_clock::now();

    int expected = cfg.producers * cfg.items;

    cout << "\n--------------- Summary ---------------\n";
    cout << "Expected Produced : " << expected << "\n";
    cout << "Actual Produced   : " << stats.produced << "\n";
    cout << "Actual Consumed   : " << stats.consumed << "\n";
    cout << "Producer Blocks   : " << stats.p_block << "\n";
    cout << "Consumer Blocks   : " << stats.c_block << "\n";
    cout << "Elapsed Time(ms)  : "
         << chrono::duration_cast<chrono::milliseconds>(end - start).count()
         << "\n";
    cout << "Result            : "
         << ((stats.produced == expected && stats.consumed == expected)
                 ? "PASS"
                 : "FAIL")
         << "\n";
    cout << "---------------------------------------\n";
}

int main() {
    // 최소 3개 이상의 설정으로 실행 결과 비교
    vector<Config> configs = {
        // 버퍼가 작아서 block이 상대적으로 자주 발생할 가능성이 큼
        {"CV Small Buffer", CV, 3, 2, 2, 3},

        // Producer 수가 많아서 버퍼가 더 빨리 차고 Producer block이 늘 가능성이 있음
        // {"Semaphore More Producers", SEM, 5, 3, 2, 8},

        // Consumer 수가 많아서 버퍼가 자주 비고 Consumer block이 늘 가능성이 있음
        // {"CV More Consumers", CV, 5, 2, 3, 8}
    };

    for (auto& c : configs) {
        run(c);
    }

    return 0;
}