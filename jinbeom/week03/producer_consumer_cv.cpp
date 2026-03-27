/*
 * Producer-Consumer with Condition Variable + Mutex (C++)
 * OSTEP Chapter 30 (Condition Variables) — 한국어판 33장
 *
 * 핵심 원칙 (OSTEP 그림 33.10):
 *  1. predicate-wait → spurious wakeup 대비 + lost notification 방지
 *     - plain wait(lock)는 notify가 wait 이전에 도착하면 영원히 block
 *     - wait(lock, pred)는 lock 보유 중 조건 확인 + 대기를 원자적으로 수행
 *  2. 두 개의 CV (not_full, not_empty) → 생산자/소비자 분리 신호
 *     - CV 하나면 생산자가 생산자를 깨우는 문제 (OSTEP 그림 33.8)
 *  3. notify_one()을 lock 보유 중 버퍼 수정 직후 호출
 *     - lock 밖에서 notify하면 출력 I/O 동안 상대방이 불필요하게 block
 *  4. 출력 전용 mutex (print_mtx)로 cout data race 방지
 *
 * 사용법: ./producer_consumer_cv [버퍼크기] [생산자수] [소비자수] [생산횟수]
 *   기본값: 버퍼=5, 생산자=2, 소비자=2, 생산횟수=10
 */

#include <iostream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <chrono>
#include <sstream>
#include <random>
#include <iomanip>
#include <cstdlib>

static std::mutex print_mtx;

static long elapsed_ms() {
    using namespace std::chrono;
    static const auto kStart = steady_clock::now();
    return duration_cast<milliseconds>(steady_clock::now() - kStart).count();
}

/* lock 보유 중에 호출 → count 읽기 안전
 * 출력은 print_mtx로 보호 → cout data race 없음
 * lock ordering: buf_mtx → print_mtx (항상 이 순서 유지) */
static void log_event(const char* role, int id, const char* event,
                      int value, int count, int capacity) {
    std::ostringstream oss;
    oss << "[" << std::setw(4) << elapsed_ms() << "ms] "
        << "[" << role << "-" << id << "] "
        << std::left << std::setw(12) << event << std::right
        << " | value=" << std::setw(5) << value
        << " | buf=" << count << "/" << capacity;

    std::lock_guard<std::mutex> plock(print_mtx);
    std::cout << oss.str() << "\n";
}

/* ── BoundedBuffer ──────────────────────────────────── */
class BoundedBuffer {
public:
    explicit BoundedBuffer(int capacity) : capacity_(capacity) {}

    /* sem_t, pthread_mutex_t와 달리 std::mutex는 복사/이동 불가
     * → Rule of Five: 명시적으로 삭제 */
    BoundedBuffer(const BoundedBuffer&)            = delete;
    BoundedBuffer& operator=(const BoundedBuffer&) = delete;
    BoundedBuffer(BoundedBuffer&&)                 = delete;
    BoundedBuffer& operator=(BoundedBuffer&&)      = delete;

    void put(int value, int producer_id) {
        std::unique_lock<std::mutex> lock(mtx_);

        /*
         * OSTEP 33.10 핵심: predicate-wait
         *
         *   wait(lock, pred) 내부 동작:
         *     1. pred() 확인 → true면 바로 리턴 (대기 없음)
         *     2. false면 lock 해제 + 대기를 원자적으로 수행
         *     3. 깨어나면 lock 재획득 후 pred() 재확인 → false면 다시 대기
         *
         *   ❌ 잘못된 방법 (Code 2 버그):
         *     lock.unlock() → printLog() → lock.lock() → wait(lock)
         *     unlock~lock 사이에 notify 도착 → lost notification → deadlock
         */
        /*
         * predicate-wait: 조건이 이미 true면 즉시 리턴, false면 lock 해제+대기를 원자적으로 수행
         * outer if 제거 → spurious wakeup에도 안전하며 wait 자체가 루프 역할
         */
        not_full_.wait(lock, [this] { return count_ < capacity_; });

        data_.push(value);
        ++count_;
        ++total_produced_;

        log_event("PROD", producer_id, "PUT", value, count_, capacity_);

        /*
         * notify는 lock 보유 중, 버퍼 수정 직후 호출
         * ❌ lock 밖에서 printLog() 이후 notify → 소비자가 불필요하게 block
         */
        not_empty_.notify_one();
    }

    int get(int consumer_id) {
        std::unique_lock<std::mutex> lock(mtx_);

        not_empty_.wait(lock, [this] { return count_ > 0; });

        int value = data_.front();
        data_.pop();
        --count_;
        ++total_consumed_;

        log_event("CONS", consumer_id, "GET", value, count_, capacity_);

        not_full_.notify_one();
        return value;
    }

    int total_produced() const { return total_produced_; }
    int total_consumed() const { return total_consumed_; }

private:
    std::queue<int>         data_;
    int                     count_          = 0;
    const int               capacity_;
    int                     total_produced_ = 0;
    int                     total_consumed_ = 0;
    std::mutex              mtx_;
    std::condition_variable not_full_;   /* 빈 슬롯 생겼을 때 → 생산자 깨움 */
    std::condition_variable not_empty_;  /* 찬 슬롯 생겼을 때 → 소비자 깨움 */
};

/* ── 스레드 함수 ────────────────────────────────────── */
static void producer_func(BoundedBuffer& buf, int id, int iterations) {
    /* thread_local: 스레드마다 독립적인 난수 엔진 → rand() data race 없음 */
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 49);

    for (int i = 0; i < iterations; i++) {
        buf.put(id * 1000 + i, id);
        std::this_thread::sleep_for(std::chrono::milliseconds(dist(rng)));
    }

    std::lock_guard<std::mutex> plock(print_mtx);
    std::cout << "[PROD-" << id << "] DONE\n";
}

static void consumer_func(BoundedBuffer& buf, int id, int iterations) {
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 79);

    for (int i = 0; i < iterations; i++) {
        buf.get(id);
        std::this_thread::sleep_for(std::chrono::milliseconds(dist(rng)));
    }

    std::lock_guard<std::mutex> plock(print_mtx);
    std::cout << "[CONS-" << id << "] DONE\n";
}

/* ── main ───────────────────────────────────────────── */
int main(int argc, char* argv[]) {
    auto parse_int = [](const char* s, int def) -> int {
        try { return std::stoi(s); }
        catch (const std::out_of_range&) {
            std::cerr << "입력값 " << s << " 이(가) 범위를 초과합니다.\n";
            std::exit(1);
        }
        catch (const std::invalid_argument&) { return def; }
    };

    int buffer_size   = (argc > 1) ? parse_int(argv[1], 5)  : 5;
    int num_producers = (argc > 2) ? parse_int(argv[2], 2)  : 2;
    int num_consumers = (argc > 3) ? parse_int(argv[3], 2)  : 2;
    int produce_count = (argc > 4) ? parse_int(argv[4], 10) : 10;

    if (buffer_size < 1 || buffer_size > 64) {
        std::cerr << "버퍼 크기는 1~64 사이여야 합니다.\n";
        return 1;
    }
    if (num_producers < 1 || num_producers > 64 ||
        num_consumers < 1 || num_consumers > 64 ||
        produce_count < 1 || produce_count > 100000) {
        std::cerr << "생산자/소비자: 1~64, 생산횟수: 1~100000 이어야 합니다.\n";
        return 1;
    }

    std::cout << "=== Condition Variable 기반 Producer-Consumer (C++) ===\n"
              << "버퍼크기=" << buffer_size
              << "  생산자=" << num_producers
              << "  소비자=" << num_consumers
              << "  생산횟수=" << produce_count << "\n\n";

    BoundedBuffer buf(buffer_size);

    /* 총 아이템을 소비자에게 균등 분배 (나머지는 앞쪽 소비자가 1씩 더 처리)
     * ❌ 잘못된 방법: total / consumers → 나머지 아이템 소비 안 됨 → hang */
    long long total_items = (long long)num_producers * produce_count;
    int base_iters  = (int)(total_items / num_consumers);
    int extra       = (int)(total_items % num_consumers);

    /* 소비자 먼저 생성 (count=0이므로 바로 wait 진입) */
    std::vector<std::thread> consumers;
    for (int i = 0; i < num_consumers; i++) {
        int iters = base_iters + (i < extra ? 1 : 0);
        consumers.emplace_back(consumer_func, std::ref(buf), i + 1, iters);
    }

    std::vector<std::thread> producers;
    for (int i = 0; i < num_producers; i++) {
        producers.emplace_back(producer_func, std::ref(buf), i + 1, produce_count);
    }

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    std::cout << "\n=== 결과 ===\n"
              << "총 생산: " << buf.total_produced()
              << "  총 소비: " << buf.total_consumed() << "\n"
              << "버퍼 오버플로우/언더플로우: "
              << (buf.total_produced() == buf.total_consumed()
                  ? "없음 (정상)" : "발생! (버그)")
              << "\n";

    return 0;
}
