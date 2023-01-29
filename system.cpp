#include <exception>
#include <utility>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <future>
#include <set>
#include <tuple>

#include "system.hpp"

void CoasterPager::wait() const {
    std::unique_lock<std::mutex> lock(mut);
    cv.wait(lock, [this] { return ready; });
}

void CoasterPager::wait(unsigned int timeout) const {
    std::unique_lock<std::mutex> lock(mut);
    std::chrono::milliseconds time(timeout);
    if (!cv.wait_for(lock, time, [this] { return ready; }))
        throw FulfillmentFailure();
}

unsigned int CoasterPager::getId() const { return id; }

bool CoasterPager::isReady() const { return ready; }

void System::run(unsigned int id, std::promise<product_ordered> &promise) {
    while (true) {
        std::vector<std::unique_ptr<Product>> completing_order;
        std::unique_lock<std::mutex> orders_lock(orders_mutex);
        unsigned int order_id = pending_orders.front();
        pending_orders.pop_front();
        auto order = std::move(pagers.find(order_id)->second);
        for (auto &product_name: order->products)
            machines_queues.find(product_name)->second.push(id);
        orders_lock.unlock();

        for (auto &product_name: order->products) {
            std::unique_lock<std::mutex> machine_lock(
                    machines_mutexes.find(product_name)->second);

            auto queue = machines_queues.find(product_name)->second;
            machines_variables.find(product_name)->
                    second.wait(machine_lock,
                                [&] { return queue.front() == id; });
            auto machine = machines.find(product_name)->second;
            try {
                completing_order.push_back(machine->getProduct());
            }
            catch (MachineFailure &) {
                //TODO
            }
            machine_lock.unlock();
        }

        order->ready = true;
        auto tuple = client_collecting.find(order->id);
        std::unique_lock<std::mutex> client_lock(get<0>(tuple->second));
        std::chrono::milliseconds time(clientTimeout);
        bool inTime = get<1>(tuple->second).wait_for(client_lock, time, [&] {
            return get<2>(tuple->second);
        });

        if (inTime) {
            promise.set_value(std::move(completing_order));
        } else {
            //TODO
        }
    }
}

System::System(machines_t machines, unsigned int numberOfWorkers,
               unsigned int clientTimeout) :
        machines(std::move(machines)),
        numberOfWorkers(numberOfWorkers),
        clientTimeout(clientTimeout) {
    for (const auto &machine: this->machines)
        menu.push_back(machine.first);

    for (unsigned int i = 0; i < numberOfWorkers; i++) {
        std::promise<product_ordered> promise;
        std::future<product_ordered> future = promise.get_future();
        std::thread worker{[&] { run(i, promise); }};
    }

}

std::vector<WorkerReport> System::shutdown() {
    //TODO
    return {};
}

std::vector<std::string> System::getMenu() const {
    return menu;
}

std::vector<unsigned int> System::getPendingOrders() const {
    std::vector<unsigned int> result;
    std::unique_lock<std::mutex> lock(orders_mutex);
    for (auto order: pending_orders) {
        result.emplace_back(order);
    }
    lock.unlock();

    return result;
}

unsigned int System::getClientTimeout() const {
    return clientTimeout;
}

std::unique_ptr<CoasterPager> System::order(std::vector<std::string> products) {
    std::unique_lock<std::mutex> lock(orders_mutex);
    std::unique_ptr<CoasterPager> order_pager(std::make_unique<CoasterPager>());
    order_pager->id = current_order_id;
    order_pager->products = std::move(products);
    current_order_id++;
    lock.unlock();

    return order_pager;
}

std::vector<std::unique_ptr<Product>>
System::collectOrder(std::unique_ptr<CoasterPager> CoasterPager) {
    return {};
}