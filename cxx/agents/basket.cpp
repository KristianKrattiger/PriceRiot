#include "basket.h"
#include "transaction.h"
#include "products.h"
#include "customer.h" // Needed for recalcBasketSize

#include <algorithm>
#include <iostream>
#include <map>
#include <unordered_map>
#include <set>
#include <numeric> // Added for std::accumulate

namespace priceriot {

Basket::Basket(std::shared_ptr<Customer> c)
    : customer_(std::move(c)), running_total_(0.0)
{}

void Basket::addProduct(const Product& p) {
    products_.push_back(p);
    running_total_ += p.price;
}

bool Basket::removeProduct(const Product& p) {
    const auto it = std::find_if(products_.begin(),
                                 products_.end(),
                                 [&](const Product& existing) {return existing.sku == p.sku;}
                                 );
    if (it == products_.end()) return false;
    running_total_ -= it->price;
    products_.erase(it);
    return true;
}

void Basket::clear() {
    products_.clear();
    running_total_ = 0.0;
}

int Basket::getSize() const {
    return static_cast<int>(products_.size());
}

double Basket::getTotal() const {
    return running_total_;
}

std::shared_ptr<Customer> Basket::getCustomer() const {
    return customer_;
}

Transaction Basket::toTransaction(int transID,
                                  int satisfaction,
                                  const std::string& ts) const {
    std::map<int,LineItem> grouped;

    for (const auto& p : products_) {
        auto it = grouped.find(p.sku);
        if (it != grouped.end()) {
            it->second.quantity   += 1;
            it->second.total      += p.price;
            it->second.name        = p.name;
        } else {
            grouped.emplace(
                p.sku,
                LineItem(p.sku, p.price, 1, p.name)
            );
        }
    }

    std::vector<LineItem> items;
    items.reserve(grouped.size());
    for (auto& [_, item] : grouped) {
        item.total = item.pricePerUnit * item.quantity;
        items.push_back(std::move(item));
    }

    return {
        customer_->getId(),
        transID,
        std::move(items),
        satisfaction,
        ts
    };
}

int estimateBasketSize(double mean, double dispersion_k, std::default_random_engine &engine) {
    if (mean <= 0) return 0;
    
    double r = dispersion_k;
    double p = dispersion_k / (dispersion_k + mean);
    int r_int = std::max(1, static_cast<int>(r)); 

    std::negative_binomial_distribution<int> nbd(r_int, p);
    int items = nbd(engine);

    return std::max(1, items); 
}

void populateBasket(Basket& basket,
                    const std::map<int, Product>& productsMap,
                    std::default_random_engine& engine) {
    
    // recalcBasketSize is defined in customer.h/.cpp within namespace priceriot
    int uniqueCount = recalcBasketSize(basket.getCustomer(), engine);
    if (uniqueCount <= 0) return;

    bool allowOverbuy = std::bernoulli_distribution(0.2)(engine);
    int overbuyBudget = 0;
    if (allowOverbuy) {
        std::uniform_int_distribution<int> overDist(1, std::max(1, uniqueCount / 2));
        overbuyBudget = overDist(engine);
    }
    const int totalBudget = uniqueCount + overbuyBudget;

    // std::cout << "[DEBUG] unique=" << uniqueCount << " overbuy=" << overbuyBudget << "\n";

    std::unordered_map<std::string, std::vector<const Product*>> byCategory;
    for (auto const& [_, p] : productsMap) {
        byCategory[p.category].push_back(&p);
    }

    std::vector<std::string> cats;
    std::vector<double>      weights;
    cats.reserve(byCategory.size());
    weights.reserve(byCategory.size());
    for (auto const& [cat, vec] : byCategory) {
        double sumPop = std::accumulate(
            vec.begin(), vec.end(), 0.0,
            [](double s, const Product* p){ return s + p->popularity; }
        );
        cats.push_back(cat);
        weights.push_back(sumPop);
    }

    if (weights.empty()) return; 

    std::discrete_distribution<size_t> catDist(weights.begin(), weights.end());
    std::uniform_int_distribution<int> qtyDist(1, 3);

    size_t numCats = 1;
    if (cats.size() > 1) {
        std::uniform_int_distribution<size_t> catCountDist(1, cats.size());
        numCats = catCountDist(engine);
    }

    std::set<std::string> pickedCats;
    // Safety break to prevent infinite loop if distribution is wonky
    int attempts = 0;
    while (pickedCats.size() < numCats && attempts < 100) {
        pickedCats.insert(cats[catDist(engine)]);
        attempts++;
    }

    int qtyCursor = 0;
    for (auto const& cat : pickedCats) {
        if (qtyCursor >= totalBudget) break;

        auto &vec = byCategory[cat];
        if (vec.empty()) continue;

        std::uniform_int_distribution<size_t> prodDist(0, vec.size() - 1);
        const Product* choice = vec[prodDist(engine)];

        int qty = qtyDist(engine);
        if (qtyCursor + qty > totalBudget) {
            qty = totalBudget - qtyCursor;
        }

        for (int i = 0; i < qty; ++i) {
            basket.addProduct(*choice);
            ++qtyCursor;
        }
    }
}

} // namespace priceriot