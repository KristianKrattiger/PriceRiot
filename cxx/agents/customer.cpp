#include "customer.h"
#include "basket.h"
#include "customer_behavior.h"
#include "transaction.h"
#include "../environment/environment.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <random>
#include <type_traits>
#include <utility>
#include <vector>

namespace priceriot {

namespace {
constexpr bool kAgentLogEnabled = false;
struct NullLogStream {
    NullLogStream(const char *, std::ios_base::openmode) {}
    explicit operator bool() const { return false; }
    template <typename T> NullLogStream &operator<<(const T &) { return *this; }
};
using AgentLogStream = std::conditional_t<kAgentLogEnabled, std::ofstream, NullLogStream>;
}

// Default ctor
Customer::Customer()
    : Agent(0),
      annualIncome(0.0), totalSpent(0.0), averageSpend(0.0), loyaltyRating(0.0), weight(0.0),
      daysAsCust(0), numPurchases(0), numReturns(0), lastPurchaseInDays(0), age(0), familySize(1),
      promotionResponse(0.0), churn(false) {}

// 4-arg ctor
Customer::Customer(const int id, double annualIncome_, int age_, std::string gender_)
    : Agent(id),
      annualIncome(annualIncome_), totalSpent(0.0), averageSpend(0.0), loyaltyRating(0.0),
      weight(0.0), daysAsCust(0), numPurchases(0), numReturns(0), lastPurchaseInDays(0), age(age_),
      familySize(1), gender(std::move(gender_)), promotionResponse(0.0), churn(false) {}

Customer::~Customer() {
    if (behavior) {
        delete behavior;
        behavior = nullptr;
    }
}

void Customer::setBehavior(ICustomerBehavior *behavior) noexcept {
    delete this->behavior;
    this->behavior = behavior;
}

// --- MAIN UPDATE LOGIC ---
bool Customer::update(const float dt, const StoreGraph &store, const Basket &basket,
                      CheckoutQueueManager *queueManager,
                      CollisionManager *collisionManager) {
    if (!behavior)
        return true;

    // Age any active navmesh path for caching / invalidation logic.
    if (behaviorState.usingNavmesh && !behaviorState.navmeshPath.empty()) {
        behaviorState.navmeshPathAgeSeconds += static_cast<double>(dt);
    } else {
        behaviorState.navmeshPathAgeSeconds = 0.0;
    }

    // 1. Setup Context
    ICustomerBehaviorContext ctx{store, basket, (double)dt, queueManager};
    ctx.collisionManager = collisionManager;

    // 2. Ask Strategy for Decision
    Decision dec = behavior->decide(*this, ctx);

    // Store for debugging / event log
    lastDecisionType = static_cast<int>(dec.type);
    lastDecisionTargetId = dec.targetId;
    lastDecisionDuration = dec.duration;

    // 3. Act on Decision
    switch (dec.type) {
        case Decision::Wait:
            if (behaviorState.dwellTicks > 0)
                behaviorState.dwellTicks--;
            break;

        case Decision::Move: {
            const double newDist = getDistOnEdge() + getSpeed() * dt;
            if (getCurrentEdgeIndex() != -1) {
                const double len = store.edgeAt(getCurrentEdgeIndex()).getLength();
                setDistOnEdge(newDist > len + 1.0 ? len : newDist);
            } else {
                setDistOnEdge(newDist);
            }
            break;
        }

        case Decision::SwitchEdge:
            setCurrentEdgeIndex(dec.targetId);
            setDistOnEdge(0.0);
            behaviorState.lastShopCell = -1;
            break;

        case Decision::PickProduct:
            behaviorState.dwellTicks += static_cast<int>(dec.duration * 60);
            break;

        case Decision::Checkout:
            break;

        case Decision::Despawn:
            // #region agent log
            { AgentLogStream lf("c:\\Users\\krist\\Projects\\PriceRiot-main\\.cursor\\debug.log", std::ios::app); if(lf) lf << "{\"hypothesisId\":\"D\",\"location\":\"customer.cpp:Despawn_handled\",\"message\":\"Despawn decision processed\",\"data\":{\"customerId\":" << getId() << ",\"prevEdgeIdx\":" << getCurrentEdgeIndex() << "},\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << "}\n"; }
            // #endregion
            setCurrentEdgeIndex(-1);
            return false;
    }

    return true;
}

void Customer::recalcWeight() {
    weight = 1.0 / (1 + std::exp(-0.1 * (loyaltyRating - 50)));
}

void Customer::calcFamSize() {
    static std::random_device rd;
    static std::mt19937 engine(rd());
    constexpr double lambda = 3.0;
    std::poisson_distribution<int> sizeDist(lambda);
    int sample = 0;
    while ((sample = sizeDist(engine)) == 0) {}
    familySize = sample;
}

int recalcBasketSize(const std::shared_ptr<Customer> &customer, std::default_random_engine &rng) {
    if (!customer)
        return 1;
    const double baseMean = 5.0 + (customer->getFamilySize() * 1.5);
    const double targetMean = baseMean * customer->getBasketSizeMultiplier();
    std::normal_distribution<double> baseDist(targetMean, 3.0);
    int totalItems = static_cast<int>(std::round(baseDist(rng)));

    std::uniform_real_distribution<double> impulseCheck(0.0, 1.0);
    if (impulseCheck(rng) < customer->getImpulsivity()) {
        std::uniform_int_distribution<int> extraItems(1, 3);
        totalItems += extraItems(rng);
    }
    return std::max(1, totalItems);
}

// --- Mutators ---
void Customer::setId(const int id) {
    Agent::setId(id);
}
void Customer::setLoyaltyRating(const double rating) {
    loyaltyRating = rating;
    recalcWeight();
}
void Customer::setTotalSpent(const double totalSpent) {
    this->totalSpent = totalSpent;
}
void Customer::setNumPurchases(const int numPurchases) {
    this->numPurchases = numPurchases;
}
void Customer::setAverageSpend(const double avg) {
    averageSpend = avg;
}
void Customer::setDwellTicks(int ticks) {
    behaviorState.dwellTicks = ticks;
}

void Customer::updateLoyalty(const int satisfaction) {
    if (satisfaction > 5)
        loyaltyRating *= 1.1;
    else
        loyaltyRating /= 1.1;
    loyaltyRating = std::max(0.0, std::min(100.0, loyaltyRating));
    recalcWeight();
}

void updateCustomerHistory(Customer *currentCust, const Transaction &newTransaction) {
    currentCust->setTotalSpent(currentCust->getTotalSpent() + newTransaction.getTotalSpent());
    currentCust->setNumPurchases(currentCust->getNumPurchases() + 1);
    const double newAvg = currentCust->getTotalSpent() / currentCust->getNumPurchases();
    currentCust->setAverageSpend(newAvg);
    currentCust->updateLoyalty(newTransaction.getSatisfaction());
}

std::pair<std::shared_ptr<Customer>, Basket>
newCustomer(std::vector<std::shared_ptr<Customer>> &customers, std::default_random_engine &engine) {
    static constexpr double meanIncome = 39.982;
    static constexpr double stdevIncome = 19.558;
    double varIncome = meanIncome * stdevIncome;
    double sigma2 = log(varIncome / (meanIncome * meanIncome) + 1.0);
    double sigma = sqrt(sigma2);
    double mu = log(meanIncome) - sigma2 / 2.0;

    std::normal_distribution<double> ageDist(38.0, 10.0);
    std::uniform_int_distribution<int> genderDist(0, 1);
    std::lognormal_distribution<double> incomeDist(mu, sigma);

    int newId = customers.empty() ? 1 : customers.back()->getId() + 1;
    int age = static_cast<int>(ageDist(engine));
    std::string gender = genderDist(engine) ? "Male" : "Female";
    double income = incomeDist(engine);

    auto newCustPtr = std::make_shared<Customer>(newId, income, age, gender);
    newCustPtr->calcFamSize();
    newCustPtr->setLoyaltyRating(50);
    newCustPtr->recalcWeight();
    customers.push_back(newCustPtr);

    Basket basket(newCustPtr);
    return std::make_pair(newCustPtr, basket);
}

std::pair<std::shared_ptr<Customer>, int>
selectCustomerWithBasketSize(std::vector<std::shared_ptr<Customer>> &customers,
                             std::default_random_engine &engine) {
    auto custPtr = selectCustomer(customers, engine);
    int size = recalcBasketSize(custPtr, engine);
    return std::make_pair(custPtr, size);
}

std::shared_ptr<Customer> selectCustomer(std::vector<std::shared_ptr<Customer>> &customers,
                                         std::default_random_engine &engine) {
    if (customers.empty())
        return nullptr;
    std::vector<double> weights;
    weights.reserve(customers.size());
    for (auto const &cptr : customers)
        weights.push_back(cptr->getWeight());
    std::discrete_distribution<size_t> dist(weights.begin(), weights.end());
    return customers[dist(engine)];
}

} // namespace priceriot