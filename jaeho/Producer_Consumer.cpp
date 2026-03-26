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
        string logMessage;

        {
            unique_lock<mutex> lock(mtx);

            while ((int)buffer.size() == maxSize) {
                lock.unlock();
                printLog("[Producer " + to_string(producerId) + "] buffer full -> wait");
                lock.lock();

                notFull.wait(lock);
            }

            buffer.push(data);
            int currentSize = buffer.size();

            logMessage = "[Producer " + to_string(producerId) +
                         "] produced: " + to_string(data) +
                         " | buffer size = " + to_string(currentSize);
        } // 여기서 buffer용 lock 해제

        printLog(logMessage);
        notEmpty.notify_one();
    }

    int get(int consumerId) {
        int data;
        string logMessage;

        {
            unique_lock<mutex> lock(mtx);

            while (buffer.empty()) {
                lock.unlock();
                printLog("[Consumer " + to_string(consumerId) + "] buffer empty -> wait");
                lock.lock();

                notEmpty.wait(lock);
            }

            data = buffer.front();
            buffer.pop();
            int currentSize = buffer.size();

            logMessage = "[Consumer " + to_string(consumerId) +
                         "] consumed: " + to_string(data) +
                         " | buffer size = " + to_string(currentSize);
        } // 여기서 buffer용 lock 해제

        printLog(logMessage);
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

    for (int i = 0; i < PRODUCER_COUNT; i++) {
        producers.push_back(thread(producer, ref(buf), i + 1, i * 100, ITEMS_PER_PRODUCER));
    }

    int itemsPerConsumer = (PRODUCER_COUNT * ITEMS_PER_PRODUCER) / CONSUMER_COUNT;
    for (int i = 0; i < CONSUMER_COUNT; i++) {
        consumers.push_back(thread(consumer, ref(buf), i + 1, itemsPerConsumer));
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