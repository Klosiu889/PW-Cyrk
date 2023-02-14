#include <atomic>
#include <chrono>
#include <deque>
#include <thread>
#include <iostream>
#include <typeinfo>

#include "system.hpp"

template <typename T, typename V>
bool checkType(const V* v) {
    return dynamic_cast<const T*>(v) != nullptr;
}

class Burger : public Product
{
};

class IceCream : public Product
{
};

class Chips : public Product
{
};

class BurgerMachine : public Machine
{
    std::atomic_uint burgersMade;
    std::chrono::seconds time = std::chrono::seconds(1);
public:
    BurgerMachine() : burgersMade(0) {}

    std::unique_ptr<Product> getProduct()
    {
        if (burgersMade > 0)
        {
            burgersMade--;
            return std::unique_ptr<Product>(new Burger());
        } else {
            std::this_thread::sleep_for(time);
            return std::unique_ptr<Product>(new Burger());
        }
    }

    void returnProduct(std::unique_ptr<Product> product)
    {
        if (!checkType<Burger>(product.get())) throw BadProductException();
        burgersMade++;
    }

    void start()
    {
        burgersMade.store(10);
    }

    void stop() {}
};

class IceCreamMachine : public Machine
{
public:
    std::unique_ptr<Product> getProduct()
    {
        throw MachineFailure();
    }

    void returnProduct(std::unique_ptr<Product> product)
    {
        if (!checkType<IceCream>(product.get())) throw BadProductException();
    }

    void start() {}

    void stop() {}
};

class ChipsMachine : public Machine
{
    std::thread thread;
    std::mutex mutex;
    std::condition_variable cond;
    std::atomic<int> wcount;
    std::deque<std::unique_ptr<Chips>> queue;
    std::atomic<bool> running;
public:
    ChipsMachine() : running(false) {}

    std::unique_ptr<Product> getProduct()
    {
        if (!running) throw MachineNotWorking();
        wcount++;
        std::unique_lock<std::mutex> lock(mutex);
        cond.wait(lock, [this](){ return !queue.empty(); });
        wcount--;
        auto product = std::move(queue.front());
        queue.pop_front();
        return product;
    }

    void returnProduct(std::unique_ptr<Product> product)
    {
        if (!checkType<Chips>(product.get())) throw BadProductException();
        if (!running) throw MachineNotWorking();
        std::lock_guard<std::mutex> lock(mutex);
        queue.push_front((std::unique_ptr<Chips>&&) (std::move(product)));
        cond.notify_one();
    }

    void start()
    {
        running = true;
        thread = std::thread([this](){
            while (running || wcount > 0)
            {
                int count = 7;
                std::this_thread::sleep_for(std::chrono::seconds(1));
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    while (count --> 0) {
                        queue.push_back(std::unique_ptr<Chips>(new Chips()));
                        cond.notify_one();
                    }
                }
            }
        });
    }

    void stop()
    {
        running = false;
        thread.join();
    }
};


int main() {
    System system{
        {
            {"burger", std::shared_ptr<Machine>(new BurgerMachine())},
            {"iceCream", std::shared_ptr<Machine>(new IceCreamMachine())},
            {"chips", std::shared_ptr<Machine>(new ChipsMachine())},
        },
        10,
        1
    };

    auto client1 = std::thread([&system]() {
        auto menu = system.getMenu();
        auto p = system.order({"burger", "chips"});
        p->wait();
        system.collectOrder(std::move(p));
        std::cout << "OK 1\n";
    });

    auto client2 = std::thread([&system](){
        system.getMenu();
        system.getPendingOrders();
        try {
            auto p = system.order({"iceCream", "chips"});
            p->wait();
            system.collectOrder(std::move(p));
        } catch (const FulfillmentFailure& e) {
            std::cout << "OK 2 -> Fulfillment\n";
        }
        catch (BadOrderException&) {
            std::cout << "OK 2 -> Bad order\n";
        }
    });

    auto client4 = std::thread([&system]() {
        auto menu = system.getMenu();
        auto p = system.order({"burger", "chips", "burger", "burger"});
        p->wait();
        system.collectOrder(std::move(p));
        std::cout << "OK 4\n";
    });

    auto client5 = std::thread([&system]() {
        auto menu = system.getMenu();
        auto p = system.order({"burger", "chips", "chips"});
        p->wait();
        system.collectOrder(std::move(p));
        std::cout << "OK 5\n";
    });

    auto client6 = std::thread([&system]() {
        auto menu = system.getMenu();
        try {
            auto p = system.order({"iceCream"});
            p->wait();
            system.collectOrder(std::move(p));
        }
        catch (FulfillmentFailure&) {
            std::cout << "OK 6 -> Fulfillment\n";
        }
        catch (BadOrderException&) {
            std::cout << "OK 6 -> Bad order\n";
        }
    });

    auto extreme_client = std::thread([&system]() {
        unsigned int size = 20;
        std::vector<std::unique_ptr<CoasterPager>> pagers;
        try {
            for (unsigned int i = 0; i < size; i++) {
                pagers.push_back(system.order({"burger", "chips"}));
            }
            for (unsigned int i = 0; i < size; i++) {
                if (i < size / 2) system.collectOrder(std::move(pagers[i]));
                else {
                    pagers[i]->wait();
                    system.collectOrder(std::move(pagers[i]));
                }
            }
        }
        catch(...){}
    });

    client1.join();
    client2.join();
    client4.join();
    client5.join();
    client6.join();
    extreme_client.join();

    auto reports = system.shutdown();

    auto client3 = std::thread([&system](){
        system.getMenu();
        system.getPendingOrders();
        try {
            auto p = system.order({"burger", "chips"});
            p->wait();
            system.collectOrder(std::move(p));
        } catch (const RestaurantClosedException& e) {
            std::cout << "OK 3\n";
        }
    });
    client3.join();

    unsigned int i = 0;
    for (auto &report: reports) {
        unsigned int j = 0;
        std::cout << "Report for worker " << i++ << std::endl;
        if (report.collectedOrders.empty() && report.abandonedOrders.empty() && report.failedOrders.empty()) {
            std::cout << "No orders" << std::endl;
            continue;
        }
        std::cout << "Collected: " << std::endl;
        for (auto &entry: report.collectedOrders) {
            std::cout << "\t" << j++ << ": ";
            for (auto &product: entry) {
                std::cout << product << " ";
            }
        }
        j = 0;
        std::cout << "\nAbandoned: " << std::endl;
        for (auto &entry: report.abandonedOrders) {
            std::cout << "\t" << j++ << ": ";
            for (auto &product: entry) {
                std::cout << product << " ";
            }
        }
        j = 0;
        std::cout << "\nFailed: " << std::endl;
        for (auto &entry: report.failedOrders) {
            std::cout << "\t" << j++ << ": ";
            for (auto &product: entry) {
                std::cout << product << " ";
            }
        }
        std::cout << "\nFailed products: " << std::endl << "\t";
        for (auto &product: report.failedProducts) {
            std::cout << product << " ";
        }
        std::cout << std::endl;
    }
}
