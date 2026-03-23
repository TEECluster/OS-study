#include <iostream>
#include <queue>
#include <thread>
#include <vector>
#include <semaphore.h>
#include <mutex>
#include <unistd.h>

using namespace std;

queue<int> buffer;
int BUFFER_SIZE = 5;
int ITEMS_PER_PRODUCER = 10;

sem_t empty_slots;
sem_t full_slots;
mutex mtx;

void producer(int id) {
    for (int i = 0; i < ITEMS_PER_PRODUCER; i++) {
        int item = id * 100 + i;

        sem_wait(&empty_slots);

        mtx.lock();
        buffer.push(item);
        cout << "[Producer " << id << "] produced: " << item
             << " | buffer size = " << buffer.size() << endl;
        mtx.unlock();

        sem_post(&full_slots);

        usleep(100000);
    }
}

void consumer(int id) {
    for (int i = 0; i < ITEMS_PER_PRODUCER; i++) {
        sem_wait(&full_slots);

        mtx.lock();
        int item = buffer.front();
        buffer.pop();
        cout << "[Consumer " << id << "] consumed: " << item
             << " | buffer size = " << buffer.size() << endl;
        mtx.unlock();

        sem_post(&empty_slots);

        usleep(150000);
    }
}

int main() {
    int producer_count = 2;
    int consumer_count = 2;

    sem_init(&empty_slots, 0, BUFFER_SIZE);
    sem_init(&full_slots, 0, 0);

    vector<thread> producers;
    vector<thread> consumers;

    for (int i = 0; i < producer_count; i++) {
        producers.push_back(thread(producer, i + 1));
    }

    for (int i = 0; i < consumer_count; i++) {
        consumers.push_back(thread(consumer, i + 1));
    }

    for (int i = 0; i < producer_count; i++) {
        producers[i].join();
    }

    for (int i = 0; i < consumer_count; i++) {
        consumers[i].join();
    }

    sem_destroy(&empty_slots);
    sem_destroy(&full_slots);

    return 0;
}