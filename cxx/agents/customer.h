#ifndef CUSTOMER_H
#define CUSTOMER_H

#include "basket.h"
#include <string>
#include <random>
#include <vector>
#include <memory>

namespace priceriot {

// Forward declarations MUST be inside the namespace
class ICustomerBehavior;
struct ICustomerBehaviorContext;
class StoreGraph;

class Customer {
public:
    Customer();
    Customer(int id, double annualIncome, int age, std::string gender);
    ~Customer();

    // --- Core Logic ---
    bool update(float dt, const StoreGraph& store, Basket& basket);

    // --- Navigation State ---
    int currentEdgeIndex = -1;
    double distOnEdge = 0.0;
    double speed = 1.0;
    
    // World position (for navmesh navigation)
    double posX = 0.0;
    double posZ = 0.0;
    
    // Getters for world position
    [[nodiscard]] double getPosX() const noexcept { return posX; }
    [[nodiscard]] double getPosZ() const noexcept { return posZ; }
    
    // Setters for world position
    void setPosition(double x, double z) noexcept { posX = x; posZ = z; }

    // --- Strategy Wiring ---
    void setBehavior(ICustomerBehavior* b) noexcept;
    [[nodiscard]] const ICustomerBehavior* getBehavior() const noexcept { return behavior; }

    // --- Getters (Stats) ---
    [[nodiscard]] int    getId()                  const { return id; }
    [[nodiscard]] double getAnnualIncome()        const { return annualIncome; }
    [[nodiscard]] double getTotalSpent()          const { return totalSpent; }
    [[nodiscard]] double getAverageSpend()        const { return averageSpend; }
    [[nodiscard]] double getLoyaltyRating()       const { return loyaltyRating; }
    [[nodiscard]] double getWeight()              const { return weight; }
    [[nodiscard]] int    getDaysAsCust()          const { return daysAsCust; }
    [[nodiscard]] int    getNumPurchases()        const { return numPurchases; }
    [[nodiscard]] int    getNumReturns()          const { return numReturns; }
    [[nodiscard]] int    getLastPurchaseInDays()  const { return lastPurchaseInDays; }
    [[nodiscard]] int    getAge()                 const { return age; }
    [[nodiscard]] int    getFamilySize()          const { return familySize; }
    [[nodiscard]] const std::string& getGender()  const { return gender; }
    [[nodiscard]] double getPromotionResponse()   const { return promotionResponse; }
    [[nodiscard]] bool   isChurn()                const { return churn; }

    // *****Behavior Profile
    [[nodiscard]] double getBasketSizeMultiplier() const { return behaviorProfile.basketSizeMultiplier; }
    [[nodiscard]] double getPriceSensitivity() const { return behaviorProfile.priceSensitivity; }
    [[nodiscard]] double getImpulsivity() const { return behaviorProfile.impulsivity; }

    enum class TripPurpose { StockUp, TopUp, Mission };
    [[nodiscard]] TripPurpose getTripPurpose() const { return behaviorProfile.tripPurpose; }

    // *****Behavior State
    [[nodiscard]] bool getBrowsing() const { return behaviorState.browsing; }
    [[nodiscard]] int  getDwellTicks() const { return behaviorState.dwellTicks; }
    [[nodiscard]] int  getLastShopCell() const { return behaviorState.lastShopCell; }

    // --- Mutators ---
    void setLastShopCell(int cell) { behaviorState.lastShopCell = cell; }
    void setDwellTicks(int ticks);
    void updateLoyalty(int satisfaction);
    void recalcWeight();
    void calcFamSize();

    void setId(int id);
    void setLoyaltyRating(double rating);
    void setTotalSpent(double total);
    void setNumPurchases(int num);
    void setAverageSpend(double avg);

    // Profile Setters
    void setBasketSizeMultiplier(double v) { behaviorProfile.basketSizeMultiplier = v; }
    void setImpulsivity(double v) { behaviorProfile.impulsivity = v; }
    void setFamilySize(int size) { this->familySize = size; }

    // State Setters
    void setSpawning(bool b) { behaviorState.isSpawning = b; }
    
    // Navmesh path state access
    bool isUsingNavmesh() const noexcept { return behaviorState.usingNavmesh; }
    void setUsingNavmesh(bool use) noexcept { behaviorState.usingNavmesh = use; }
    const std::vector<std::pair<double, double>>& getNavmeshPath() const noexcept { return behaviorState.navmeshPath; }
    void setNavmeshPath(const std::vector<std::pair<double, double>>& path) { behaviorState.navmeshPath = path; behaviorState.currentWaypointIndex = 0; }
    size_t getCurrentWaypointIndex() const noexcept { return behaviorState.currentWaypointIndex; }
    void setCurrentWaypointIndex(size_t idx) noexcept { behaviorState.currentWaypointIndex = idx; }
    void incrementWaypointIndex() noexcept { behaviorState.currentWaypointIndex++; }

private:
    struct BehaviorProfile {
        double basketSizeMultiplier = 1.0;
        double priceSensitivity = 0.5;
        double impulsivity = 0.35;
        TripPurpose tripPurpose = TripPurpose::TopUp;
        double budgetPerTripMean = 35.0;
        double budgetPerTripSigma = 12.0;
    };

    struct BehaviorState {
        bool isSpawning = false;
        bool browsing = true;
        int dwellTicks = 0;
        int lastShopCell = -1;
        int targetNodeId = -1;
        
        // Navmesh path state
        std::vector<std::pair<double, double>> navmeshPath; // Waypoints (x, z)
        size_t currentWaypointIndex = 0;
        bool usingNavmesh = false;
    };

    BehaviorProfile behaviorProfile;
    BehaviorState behaviorState;

    ICustomerBehavior* behavior = nullptr;

    int id;
    double annualIncome;
    double totalSpent;
    double averageSpend;
    double loyaltyRating;
    double weight;
    int daysAsCust;
    int numPurchases;
    int numReturns;
    int lastPurchaseInDays;
    int age;
    int familySize;
    std::string gender;
    double promotionResponse;
    bool churn;
};

// --- Free Functions ---
int recalcBasketSize(const std::shared_ptr<Customer>& customer, std::default_random_engine& rng);

std::pair<std::shared_ptr<Customer>, Basket>
newCustomer(std::vector<std::shared_ptr<Customer>> &customers,
                 std::default_random_engine &engine);

std::pair<std::shared_ptr<Customer>, int>
selectCustomerWithBasketSize(
    std::vector<std::shared_ptr<Customer>>& customers,
    std::default_random_engine& engine
);

std::shared_ptr<Customer> selectCustomer(
    std::vector<std::shared_ptr<Customer>>& customers,
    std::default_random_engine& engine
);

void updateCustomerHistory(Customer* currentCust, const class Transaction& newTransaction);

} // namespace priceriot

#endif // CUSTOMER_H