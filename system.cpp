#include <exception>
#include <utility>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <unordered_map>
#include <functional>
#include <future>
#include <set>

#include "system.hpp"

void run() {

}

void CoasterPager::wait() const {
    std::unique_lock<std::mutex> lock(mut);
    cv.wait(lock, [this] { return ready; } );
}

void CoasterPager::wait(unsigned int timeout) const {
    std::unique_lock<std::mutex> lock(mut);
    std::chrono::milliseconds time(timeout);
    if (!cv.wait_for(lock, time, [this] { return ready; } ))
        throw FulfillmentFailure();
}

unsigned int CoasterPager::getId() const { return id; }

bool CoasterPager::isReady() const { return ready; }

System::System(machines_t machines, unsigned int numberOfWorkers,
               unsigned int clientTimeout):
    machines(std::move(machines)),
    numberOfWorkers(numberOfWorkers),
    clientTimeout(clientTimeout)
{
    for (const auto& machine: machines)
        menu.push_back(machine.first);

    for (unsigned int i = 0; i < numberOfWorkers; i++)
        workers.emplace_back(run);
}

std::vector<std::string> System::getMenu() const {
    return menu;
}

std::vector<unsigned int> System::getPendingOrders() const {
    std::vector<unsigned int> result;
    std::unique_lock<std::mutex> lock(queue_mutex);
    std::copy(pendingOrders.begin(), pendingOrders.end(), result.begin());
    lock.unlock();

    return result;
}

unsigned int System::getClientTimeout() const {
    return clientTimeout;
}

std::unique_ptr<CoasterPager> System::order(std::vector<std::string> products) {

}