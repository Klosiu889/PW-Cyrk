#ifndef SYSTEM_HPP
#define SYSTEM_HPP

#include <exception>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <queue>
#include <vector>
#include <unordered_map>
#include <map>
#include <list>

#include "machine.hpp"

class FulfillmentFailure : public std::exception {
};

class OrderNotReadyException : public std::exception {
};

class BadOrderException : public std::exception {
};

class BadPagerException : public std::exception {
};

class OrderExpiredException : public std::exception {
};

class RestaurantClosedException : public std::exception {
};

struct WorkerReport {
    std::vector<std::vector<std::string>> collectedOrders;
    std::vector<std::vector<std::string>> abandonedOrders;
    std::vector<std::vector<std::string>> failedOrders;
    std::vector<std::string> failedProducts;
};

class CoasterPager {
private:
    friend class System;

    bool ready{};
    unsigned int id{};
    std::vector<std::string> products;
    mutable std::mutex mut;
    mutable std::condition_variable cv;
public:
    void wait() const;

    void wait(unsigned int timeout) const;

    [[nodiscard]] unsigned int getId() const;

    [[nodiscard]] bool isReady() const;
};

class System {
public:
    typedef std::unordered_map<std::string, std::shared_ptr<Machine>> machines_t;

    System(machines_t machines, unsigned int numberOfWorkers,
           unsigned int clientTimeout);

    std::vector<WorkerReport> shutdown();

    std::vector<std::string> getMenu() const;

    std::vector<unsigned int> getPendingOrders() const;

    std::unique_ptr<CoasterPager> order(std::vector<std::string> products);

    std::vector<std::unique_ptr<Product>>
    collectOrder(std::unique_ptr<CoasterPager> CoasterPager);

    unsigned int getClientTimeout() const;

private:
    using product_ordered = std::vector<std::unique_ptr<Product>>;

    machines_t machines;
    unsigned int numberOfWorkers;
    unsigned int clientTimeout;
    std::vector<std::string> menu;
    std::vector<std::thread> workers;

    unsigned int current_order_id{};
    std::list<unsigned int> pending_orders;
    std::map<unsigned int, std::unique_ptr<CoasterPager>> pagers;
    std::map<std::string, std::queue<unsigned int>> machines_queues;
    std::map<std::string, std::condition_variable> machines_variables;
    std::map<std::string, std::mutex> machines_mutexes;
    std::map<unsigned int, std::tuple<std::mutex, std::condition_variable, bool>> client_collecting;
    std::map<unsigned int, std::future<product_ordered>> futures;

    mutable std::mutex workers_mutex;
    mutable std::mutex orders_mutex;

    void run(unsigned int id, std::promise<product_ordered> &promise);
};

#endif // SYSTEM_HPP