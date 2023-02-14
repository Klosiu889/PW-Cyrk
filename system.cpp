#include <exception>
#include <utility>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <future>
#include <set>
#include <iostream>
#include <algorithm>
#include <typeinfo>

#include "system.hpp"

void CoasterPager::wait() const {
    std::unique_lock<std::mutex> lock(mut);
    cv->wait(lock, [this] { return *status != OrderStatus::IN_PROGRES; });

    if (*status == OrderStatus::FAILED) throw FulfillmentFailure();
}

void CoasterPager::wait(unsigned int timeout) const {
    std::unique_lock<std::mutex> lock(mut);
    auto now = std::chrono::system_clock::now();
    std::chrono::milliseconds time(timeout);
    cv->wait_until(lock, now + time,
                   [this] { return *status != OrderStatus::IN_PROGRES; });

    if (*status == OrderStatus::FAILED) throw FulfillmentFailure();
}

unsigned int CoasterPager::getId() const { return id; }

bool CoasterPager::isReady() const { return *status == OrderStatus::READY; }

void System::collectProduct(unsigned int order_id, unsigned int my_id,
                            std::string &product_name,
                            WorkerReport &report,
                            std::vector<std::pair<std::string, std::unique_ptr<Product>>> &collecting,
                            std::mutex &mutex) {
    std::unique_lock<std::mutex> lock(machines_data[product_name]->mut);

    if (machines_data[product_name]->waiting.empty() ||
        machines_data[product_name]->waiting.front() !=
        std::make_pair(order_id, my_id))
        machines_data[product_name]->cv.wait(lock, [&, this] {
            return machines_data[product_name]->waiting.empty() ||
                   machines_data[product_name]->waiting.front() !=
                   std::make_pair(order_id, my_id);
        });

    try {
        auto product = machines[product_name]->getProduct();
        std::unique_lock<std::mutex> lock2(mutex);
        collecting.emplace_back(product_name, std::move(product));
        lock2.unlock();
    }
    catch (...) {
        std::unique_lock<std::mutex> lock2(menu_mutex);
        menu.erase(product_name);
        lock2.unlock();
        std::unique_lock<std::mutex> lock3(mutex);
        report.failedProducts.push_back(product_name);
        lock3.unlock();
    }

    machines_data[product_name]->waiting.pop();
    machines_data[product_name]->cv.notify_all();
}

void System::run() {
    WorkerReport report;

    while (!(!is_open && pending_orders.empty())) {
        std::vector<std::pair<std::string, std::unique_ptr<Product>>> collecting;
        std::mutex mutex;
        std::unique_lock<std::mutex> lock(pending_orders_mutex);
        pending_orders_cv.wait(lock,
                               [this] { return !pending_orders.empty(); });

        auto id = pending_orders.front();

        if (id == -1) break;

        pending_orders.pop_front();
        auto products = std::move(orders_products[id]);

        unsigned int thread_id = 0;
        std::vector<std::thread> threads;
        for (auto &product: products) {
            machines_data[product]->waiting.emplace(id, thread_id);
            std::thread thread{
                    [id, thread_id, &product, &report, &collecting, &mutex, this] {
                        collectProduct(id, thread_id, product, report,
                                       collecting, mutex);
                    }};
            threads.emplace_back(std::move(thread));
            thread_id++;
        }

        lock.unlock();

        for (auto &thread: threads) {
            thread.join();
        }

        std::unique_lock<std::mutex> lock2(orders_mutex);
        *orders_data[id]->status = (collecting.size() == products.size()) ? OrderStatus::READY : OrderStatus::FAILED;

        orders_data[id]->pager_cv->notify_all();

        auto now = std::chrono::system_clock::now();
        std::chrono::milliseconds time(clientTimeout);
        bool collection = true;
        if (*orders_data[id]->status == OrderStatus::READY)
            collection = orders_data[id]->cv.wait_until(lock2, now + time,
                                                         [this, id] {
                                                             return orders_data[id]->client_collecting;
                                                         });

        if (!collection) *orders_data[id]->status = OrderStatus::EXPIRED;
        if (*orders_data[id]->status == OrderStatus::READY) {
            for (auto &pair: collecting) {
                orders_data[id]->completed.push_back(std::move(pair.second));
            }
        }

        orders_data[id]->ready_to_collect = true;
        orders_data[id]->collect_cv.notify_all();

        OrderStatus status = *orders_data[id]->status;

        lock.lock();
        orders_in_progress.erase(id);
        lock.unlock();

        lock2.unlock();

        switch (status) {
            case OrderStatus::READY:
                report.collectedOrders.push_back(products);
                break;
            case OrderStatus::FAILED:
                report.failedOrders.push_back(products);
                break;
            case OrderStatus::EXPIRED:
                report.abandonedOrders.push_back(products);
                break;
            default:
                break;
        }

        if (status == OrderStatus::FAILED || status == OrderStatus::EXPIRED) {
            std::unique_lock<std::mutex> lock3(machines_mutex);
            for (auto &pair: collecting) {
                machines[pair.first]->returnProduct(std::move(pair.second));
            }
            lock3.unlock();
        }
    }

    std::unique_lock<std::mutex> lock(reports_mutex);
    reports.push_back(std::move(report));
    lock.unlock();
}

System::System(machines_t machines, unsigned int numberOfWorkers,
               unsigned int clientTimeout) :
        is_open(true),
        machines(std::move(machines)),
        numberOfWorkers(numberOfWorkers),
        clientTimeout(clientTimeout) {
    for (const auto &machine: this->machines) {
        menu.insert(machine.first);
        machines_data.insert(
                std::make_pair(machine.first, std::make_shared<MachineData>()));
        machine.second->start();
    }

    for (unsigned int i = 0; i < numberOfWorkers; i++) {
        workers.emplace_back([this] { run(); });
    }
}

std::vector<WorkerReport> System::shutdown() {
    if (!is_open) return std::move(reports);
    reports.clear();

    std::unique_lock<std::mutex> lock(pending_orders_mutex);
    is_open = false;
    pending_orders.push_back(-1);
    pending_orders_cv.notify_all();
    lock.unlock();
    for (auto &worker: workers) {
        worker.join();
    }

    for (auto &machine: machines) {
        machine.second->stop();
    }

    menu.clear();

    return std::move(reports);
}

std::vector<std::string> System::getMenu() const {
    std::vector<std::string> result;
    std::unique_lock<std::mutex> lock(menu_mutex);
    for (auto &product: menu) {
        result.emplace_back(product);
    }
    lock.unlock();

    return result;
}

std::vector<unsigned int> System::getPendingOrders() const {
    std::vector<unsigned int> result;
    std::unique_lock<std::mutex> lock(pending_orders_mutex);
    for (auto order: orders_in_progress) {
        result.emplace_back(order);
    }
    lock.unlock();

    return result;
}

unsigned int System::getClientTimeout() const {
    return clientTimeout;
}

std::unique_ptr<CoasterPager> System::order(std::vector<std::string> products) {
    std::unique_lock<std::mutex> lock(pending_orders_mutex);
    if (!is_open) throw RestaurantClosedException();
    lock.unlock();

    std::unique_lock<std::mutex> lock2(menu_mutex);
    bool proper_order = std::all_of(products.begin(), products.end(),
                                    [this](const std::string &product) {
                                        return menu.find(product) != menu.end();
                                    });
    lock2.unlock();

    if (!proper_order) throw BadOrderException();

    std::unique_lock<std::mutex> lock3(orders_mutex);

    auto order_pager = std::make_unique<CoasterPager>();
    auto order = std::make_shared<OrderData>();

    order_pager->id = current_order_id++;
    order->status = order_pager->status;
    order->pager_cv = order_pager->cv;

    orders_data.insert(std::make_pair(order_pager->id, order));
    orders_products.insert(
            std::make_pair(order_pager->id, std::move(products)));

    lock3.unlock();

    lock.lock();

    pending_orders.push_back(order_pager->id);
    pending_orders_cv.notify_all();
    orders_in_progress.insert(order_pager->id);

    lock.unlock();

    return order_pager;
}

std::vector<std::unique_ptr<Product>>
System::collectOrder(std::unique_ptr<CoasterPager> CoasterPager) {
    auto id = CoasterPager->id;

    std::unique_lock<std::mutex> lock(orders_mutex);

    auto order = orders_data.find(id);
    if (order == orders_data.end()) throw BadPagerException();

    switch (*order->second->status) {
        case READY:
            break;
        case IN_PROGRES:
            throw OrderNotReadyException();
        case FAILED:
            throw FulfillmentFailure();
        case EXPIRED:
            throw OrderExpiredException();
    }

    orders_data[id]->client_collecting = true;
    orders_data[id]->cv.notify_all();

    orders_data[id]->collect_cv.wait(lock, [id, this]{ return orders_data[id]->ready_to_collect; });

    auto result = std::move(orders_data[id]->completed);
    orders_data.erase(id);

    return result;
}