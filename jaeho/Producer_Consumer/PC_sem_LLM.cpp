#include <iostream>
#include <queue>
#include <thread>
#include <vector>
#include <semaphore.h>
#include <mutex>
#include <unistd.h>

using namespace std;

queue<int> buffer;

const int BUFFER_SIZE = 5;
const int ITEMS_PER_PRODUCER = 10;
const int PRODUCER_COUNT = 2;
const int CONSUMER_COUNT = 2;

sem_t empty_slots;
sem_t full_slots;
mutex mtx;

void producer(int id) {
    for (int i = 0; i < ITEMS_PER_PRODUCER; i++) {
        int item = id * 100 + i;

        sem_wait(&empty_slots);

        {
            lock_guard<mutex> lock(mtx);
            buffer.push(item);
            cout << "[Producer " << id << "] produced: " << item
                 << " | buffer size = " << buffer.size() << endl;
        }

        sem_post(&full_slots);

        usleep(100000);
    }
}

void consumer(int id, int consume_count) {
    for (int i = 0; i < consume_count; i++) {
        sem_wait(&full_slots);

        {
            lock_guard<mutex> lock(mtx);
            int item = buffer.front();
            buffer.pop();
            cout << "[Consumer " << id << "] consumed: " << item
                 << " | buffer size = " << buffer.size() << endl;
        }

        sem_post(&empty_slots);

        usleep(150000);
    }
}

int main() {
    sem_init(&empty_slots, 0, BUFFER_SIZE);
    sem_init(&full_slots, 0, 0);

    vector<thread> producers;
    vector<thread> consumers;

    int total_items = PRODUCER_COUNT * ITEMS_PER_PRODUCER;
    int base_items_per_consumer = total_items / CONSUMER_COUNT;
    int extra_items = total_items % CONSUMER_COUNT;

    for (int i = 0; i < PRODUCER_COUNT; i++) {
        producers.emplace_back(producer, i + 1);
    }

    for (int i = 0; i < CONSUMER_COUNT; i++) {
        int consume_count = base_items_per_consumer + (i < extra_items ? 1 : 0);
        consumers.emplace_back(consumer, i + 1, consume_count);
    }

    for (auto& t : producers) {
        t.join();
    }

    for (auto& t : consumers) {
        t.join();
    }

    sem_destroy(&empty_slots);
    sem_destroy(&full_slots);

    cout << "\nAll producer and consumer threads finished.\n";

    return 0;
}