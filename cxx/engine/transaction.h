#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <random>
#include "products.h"

namespace priceriot {

    class Customer;

    struct LineItem {
        int id{};
        double pricePerUnit{};
        int quantity{};
        double total{};
        std::string name{};

        LineItem(int id, double ppu, int qty,std::string name)
         : id(id)
         , pricePerUnit(ppu)
         , quantity(qty)
         , name(std::move(name))
         , total(ppu * qty)
        {}
    };

    class Transaction {
    public:
        // Constructors
        Transaction();
        Transaction(int custID, int transID);
        Transaction(int custID_, int transID_, std::vector<LineItem> items_,
                    int satisfaction_, std::string timestamp_);

        // Getters
        [[nodiscard]] int getCustID() const;
        [[nodiscard]] int getTransID() const;
        [[nodiscard]] int getSatisfaction() const;
        [[nodiscard]] std::string getTimestamp() const;
        [[nodiscard]] const std::vector<LineItem>& getItems() const;
        [[nodiscard]] double getTotalSpent() const;

        // Setters
        void setCustID(int c);
        void setTransID(int id);
        void setSatisfaction(int s);
        void setTimestamp(const std::string& ts);
        void setItems(const std::vector<LineItem>& newItems);

        // Factory: builds a random Transaction
        static Transaction randomTransaction(
            std::vector<std::shared_ptr<Customer>>& customers,
            const std::vector<Transaction>& transactions,
            const std::map<int, Product>& productsMap,
            std::default_random_engine &engine
        );

    private:
        int custID;
        int transID;
        std::vector<LineItem> items;
        int satisfaction;
        std::string timestamp;
        double totalSpent; // Added field to match usage in getter/constructor
    };

} // namespace priceriot

#endif // TRANSACTION_H