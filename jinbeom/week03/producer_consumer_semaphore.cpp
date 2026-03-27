/*
 * Producer-Consumer with Semaphore (C++)
 * OSTEP Chapter 31 (세마포어) — 한국어판 34장
 *
 * 핵심 원칙 (OSTEP 그림 34.12):
 *  - empty/full 세마포어를 mutex보다 먼저 wait → 교착 상태 방지
 *    ❌ 잘못된 예 (그림 34.11): mutex 먼저 잡으면 교착 상태 발생
 *  - mutex는 버퍼 접근만 보호 (범위 최소화)
 *  - sem_post는 mutex 해제 직후, 출력 전에 호출
 *    ❌ 잘못된 예 (Code 4 버그): sem_post를 cout 이후에 호출 → 소비자 불필요하게 block
 *  - std::mutex (RAII) 사용 → pthread_mutex_t 대비 예외 안전성 보장
 *  - thread_local std::mt19937 → rand() data race 없음
 *
 * 사용법: ./producer_consumer_semaphore [버퍼크기] [생산자수] [소비자수] [생산횟수]
 *   기본값: 버퍼=5, 생산자=2, 소비자=2, 생산횟수=10
 */

#include <iostream>
#include <queue>
#include <thread>
#include <mutex>
#include <vector>
#include <chrono>
#include <sstream>
#include <random>
#include <iomanip>
#include <cstdlib>
#include <atomic>
#include <stdexcept>
#include <semaphore.h>

static std::mutex print_mtx;

static long elapsed_ms() {
    using namespace std::chrono;
    static const auto kStart = steady_clock::now();
    return duration_cast<milliseconds>(steady_clock::now() - kStart).count();
}

/* lock 보유 중에 호출 → count 읽기 안전
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
    explicit BoundedBuffer(int capacity) : capacity_(capacity) {
        /*
         * OSTEP 그림 34.12 초기값:
         *  empty = capacity : 처음엔 모든 슬롯이 비어 있음
         *  full  = 0        : 처음엔 꺼낼 데이터 없음
         */
        if (sem_init(&empty_, 0, capacity) != 0)
            throw std::runtime_error("sem_init(empty) failed");
        if (sem_init(&full_, 0, 0) != 0) {
            sem_destroy(&empty_);
            throw std::runtime_error("sem_init(full) failed");
        }
    }

    ~BoundedBuffer() {
        sem_destroy(&empty_);
        sem_destroy(&full_);
    }

    BoundedBuffer(const BoundedBuffer&)            = delete;
    BoundedBuffer& operator=(const BoundedBuffer&) = delete;
    BoundedBuffer(BoundedBuffer&&)                 = delete;
    BoundedBuffer& operator=(BoundedBuffer&&)      = delete;

    void put(int value, int producer_id) {
        /*
         * OSTEP 34.12 올바른 순서:
         *   P1  sem_wait(empty)   — 빈 슬롯 확보 (mutex 밖에서 block)
         *   P2  mutex_lock        — 임계 구역 진입 (범위를 좁게!)
         *   P3  push + log        — 버퍼 수정 (count 정확)
         *   P4  mutex_unlock      — 임계 구역 탈출
         *   P5  sem_post(full)    — 소비자 깨우기 (출력 전에!)
         *
         * ❌ 잘못된 순서 (그림 34.11): mutex → sem_wait → ... → 교착 상태
         * ❌ sem_post를 cout 이후에 호출 (Code 4 버그) → 소비자 block 지연
         *
         * 참고: sem_wait 전 sem_getvalue로 block 예상 여부 확인하지만,
         *       TOCTOU 특성상 로그는 근사치임.
         */
        int val;
        sem_getvalue(&empty_, &val);
        if (val <= 0)
            log_event("PROD", producer_id, "WAIT(full)", value, count_, capacity_);

        sem_wait(&empty_);                              /* P1 */

        if (val <= 0)
            log_event("PROD", producer_id, "WAKEUP", value, count_, capacity_);

        {
            std::lock_guard<std::mutex> lock(mtx_);    /* P2 */
            data_.push(value);
            ++count_;
            ++total_produced_;
            log_event("PROD", producer_id, "PUT", value, count_, capacity_);
        }                                               /* P4: lock 해제 */

        sem_post(&full_);                               /* P5: 소비자 깨우기 */
    }

    int get(int consumer_id) {
        /*
         * OSTEP 34.12 소비자 순서:
         *   C1  sem_wait(full)    — 채워진 슬롯 확보 (mutex 밖에서 block)
         *   C2  mutex_lock        — 임계 구역 진입
         *   C3  pop + log         — 버퍼 수정
         *   C4  mutex_unlock      — 임계 구역 탈출
         *   C5  sem_post(empty)   — 생산자 깨우기 (출력 전에!)
         */
        int val;
        sem_getvalue(&full_, &val);
        if (val <= 0)
            log_event("CONS", consumer_id, "WAIT(empty)", -1, count_, capacity_);

        sem_wait(&full_);                               /* C1 */

        if (val <= 0)
            log_event("CONS", consumer_id, "WAKEUP", -1, count_, capacity_);

        int value;
        {
            std::lock_guard<std::mutex> lock(mtx_);    /* C2 */
            value = data_.front();
            data_.pop();
            --count_;
            ++total_consumed_;
            log_event("CONS", consumer_id, "GET", value, count_, capacity_);
        }                                               /* C4: lock 해제 */

        sem_post(&empty_);                              /* C5: 생산자 깨우기 */

        return value;
    }

    int total_produced() const { return total_produced_; }
    int total_consumed() const { return total_consumed_; }

private:
    std::queue<int>      data_;
    std::atomic<int>     count_{0};       /* atomic: mutex 밖 WAIT 로그에서 읽어도 안전 */
    const int            capacity_;
    int                  total_produced_ = 0;
    int                  total_consumed_ = 0;
    std::mutex           mtx_;
    sem_t                empty_;          /* 빈 슬롯 수: 초기값 = capacity */
    sem_t                full_;           /* 채워진 슬롯 수: 초기값 = 0 */
};

/* ── 스레드 함수 ────────────────────────────────────── */
static void producer_func(BoundedBuffer& buf, int id, int iterations) {
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

    std::cout << "=== Semaphore 기반 Producer-Consumer (C++) ===\n"
              << "버퍼크기=" << buffer_size
              << "  생산자=" << num_producers
              << "  소비자=" << num_consumers
              << "  생산횟수=" << produce_count << "\n\n";

    BoundedBuffer buf(buffer_size);

    long long total_items = (long long)num_producers * produce_count;
    int base_iters  = (int)(total_items / num_consumers);
    int extra       = (int)(total_items % num_consumers);

    /* 소비자 먼저 생성 (full=0이므로 바로 block) */
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
