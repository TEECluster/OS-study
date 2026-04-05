#include <iostream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <chrono>
#include <string>

using namespace std;

class BoundedBuffer {
private:
    queue<int> buffer;
    int maxSize;

    mutex mtx;
    mutex printMtx;
    condition_variable notFull;
    condition_variable notEmpty;

    void printLog(const string& message) {
        lock_guard<mutex> lock(printMtx);
        cout << message << '\n';
    }

public:
    BoundedBuffer(int size) : maxSize(size) {}

    void put(int data, int producerId) {
        {
            unique_lock<mutex> lock(mtx);

            if ((int)buffer.size() == maxSize) {
                printLog("[Producer " + to_string(producerId) + "] buffer full -> wait");
            }

            notFull.wait(lock, [this]() {
                return (int)buffer.size() < maxSize;
            });

            buffer.push(data);

            string logMessage = "[Producer " + to_string(producerId) +
                                "] produced: " + to_string(data) +
                                " | buffer size = " + to_string(buffer.size());

            printLog(logMessage);
        }

        notEmpty.notify_one();
    }

    int get(int consumerId) {
        int data;

        {
            unique_lock<mutex> lock(mtx);

            if (buffer.empty()) {
                printLog("[Consumer " + to_string(consumerId) + "] buffer empty -> wait");
            }

            notEmpty.wait(lock, [this]() {
                return !buffer.empty();
            });

            data = buffer.front();
            buffer.pop();

            string logMessage = "[Consumer " + to_string(consumerId) +
                                "] consumed: " + to_string(data) +
                                " | buffer size = " + to_string(buffer.size());

            printLog(logMessage);
        }

        notFull.notify_one();
        return data;
    }
};

void producer(BoundedBuffer& buf, int producerId, int startValue, int count) {
    for (int i = 0; i < count; i++) {
        int data = startValue + i;
        buf.put(data, producerId);
        this_thread::sleep_for(chrono::milliseconds(100));
    }
}

void consumer(BoundedBuffer& buf, int consumerId, int count) {
    for (int i = 0; i < count; i++) {
        buf.get(consumerId);
        this_thread::sleep_for(chrono::milliseconds(150));
    }
}

int main() {
    const int BUFFER_SIZE = 5;
    const int PRODUCER_COUNT = 2;
    const int CONSUMER_COUNT = 2;
    const int ITEMS_PER_PRODUCER = 10;

    BoundedBuffer buf(BUFFER_SIZE);

    vector<thread> producers;
    vector<thread> consumers;

    int totalItems = PRODUCER_COUNT * ITEMS_PER_PRODUCER;
    int baseItemsPerConsumer = totalItems / CONSUMER_COUNT;
    int extraItems = totalItems % CONSUMER_COUNT;

    for (int i = 0; i < PRODUCER_COUNT; i++) {
        producers.emplace_back(producer, ref(buf), i + 1, i * 100, ITEMS_PER_PRODUCER);
    }

    for (int i = 0; i < CONSUMER_COUNT; i++) {
        int consumeCount = baseItemsPerConsumer + (i < extraItems ? 1 : 0);
        consumers.emplace_back(consumer, ref(buf), i + 1, consumeCount);
    }

    for (auto& t : producers) {
        t.join();
    }

    for (auto& t : consumers) {
        t.join();
    }

    cout << "\nAll producer and consumer threads finished.\n";
    return 0;
}