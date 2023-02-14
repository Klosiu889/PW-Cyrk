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
#include <set>

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

enum OrderStatus {
    READY,
    IN_PROGRES,
    FAILED,
    EXPIRED
};

class CoasterPager {
private:
    friend class System;

    std::shared_ptr<OrderStatus> status{
            std::make_shared<OrderStatus>(IN_PROGRES)};
    unsigned int id{};
    mutable std::mutex mut;
    mutable std::shared_ptr<std::condition_variable> cv{
            std::make_shared<std::condition_variable>()};
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
    struct OrderData {
        std::vector<std::unique_ptr<Product>> completed;
        std::shared_ptr<OrderStatus> status;
        mutable std::mutex mut;
        mutable std::condition_variable cv;
        mutable std::shared_ptr<std::condition_variable> pager_cv;
        bool client_collecting{false};
        mutable std::condition_variable collect_cv;
        bool ready_to_collect{false};
    };

    struct MachineData {
        mutable std::mutex mut;
        mutable std::condition_variable cv;
        std::queue<std::pair<unsigned int, unsigned int>> waiting;
    };

    bool is_open;

    machines_t machines;
    unsigned int numberOfWorkers;
    unsigned int clientTimeout;
    mutable std::mutex menu_mutex;
    std::set<std::string> menu;
    std::vector<std::thread> workers;
    mutable std::mutex reports_mutex;
    std::vector<WorkerReport> reports;

    std::mutex machines_mutex;
    std::map<std::string, std::shared_ptr<MachineData>> machines_data;

    mutable std::mutex pending_orders_mutex;
    mutable std::condition_variable pending_orders_cv;
    unsigned int current_order_id{};
    std::list<long long> pending_orders;
    std::map<unsigned int, std::vector<std::string>> orders_products;

    mutable std::mutex orders_mutex;
    std::map<unsigned int, std::shared_ptr<OrderData>> orders_data;

    void collectProduct(unsigned int order_id, unsigned int my_id,
                        std::string &product_name,
                        WorkerReport &report,
                        std::vector<std::pair<std::string, std::unique_ptr<Product>>> &collecting,
                        std::mutex &mutex);

    void run();
};

#endif // SYSTEM_HPP