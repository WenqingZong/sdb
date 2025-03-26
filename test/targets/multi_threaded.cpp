#include <iostream>
#include <pthread.h>
#include <unistd.h>
#include <vector>

void* say_hi(void*) {
    // Give the debugger time to catch each thread at the breakpoint
    sleep(1);
    std::cout << "Thread " << gettid() << " reporting in\n";
    return nullptr;
}

int main() {
    std::vector<pthread_t> threads(10);

    for (auto& thread : threads) {
        pthread_create(&thread, nullptr, say_hi, nullptr);
    }

    for (auto& thread : threads) {
        pthread_join(thread, nullptr);
    }

    return 0;
}
