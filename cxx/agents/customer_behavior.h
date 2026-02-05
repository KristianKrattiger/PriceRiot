/**
 * @file customer_behavior.h
 * @brief Strategy pattern for customer AI: ICustomerBehavior and concrete behaviors.
 *
 * Each tick, Customer::update() calls behavior->decide() which returns a Decision
 * (Move, PickProduct, Checkout, etc.). DefaultBehavior (browsing) and MissionBehavior
 * (targeted SKU list) implement different shopping patterns.
 */
#ifndef CUSTOMER_BEHAVIOR_H
#define CUSTOMER_BEHAVIOR_H

#include <vector>

namespace priceriot {

// Forward declarations inside namespace
class StoreGraph;
class Basket;
class Customer;
class CheckoutQueueManager;

/** Context passed to decide(): store graph, basket, delta time, and optional queue manager. */
struct ICustomerBehaviorContext {
    const StoreGraph &store;
    const Basket &basket;
    double dt;
    CheckoutQueueManager *queueManager = nullptr; // Optional: for checkout queue integration
};

/** Output of ICustomerBehavior::decide(): action type and optional target. */
struct Decision {
    enum DecisionType { Move, SwitchEdge, PickProduct, Wait, Checkout, Despawn } type = Move;
    int targetId = -1;
    float duration = 0.0f;
};

/** Abstract base for customer decision logic. Subclass to implement different shopping behaviors. */
class ICustomerBehavior {
  public:
    virtual ~ICustomerBehavior() = default;
    virtual void onEnterStore(Customer &c, const ICustomerBehaviorContext &ctx) const {}
    virtual Decision decide(Customer &c, const ICustomerBehaviorContext &ctx) = 0;
    /** Current FSM state name for debugging (e.g. "Entering", "Browsing"). */
    virtual const char *getStateName() const { return "Unknown"; }
    /** Behavior type label for UI/log (e.g. "Default", "Mission"). */
    virtual const char *getBehaviorType() const { return "Unknown"; }
};

/** Browsing behavior: prefers aisles, impulsivity-scaled picks, state machine through store. */
class DefaultBehavior : public ICustomerBehavior {
  public:
    enum State { Entering, Browsing, HeadingToCheckout, InQueue, HeadingToExit, Exiting, Done };

    DefaultBehavior();

    void onEnterStore(Customer &c, const ICustomerBehaviorContext &ctx) const override;
    Decision decide(Customer &c, const ICustomerBehaviorContext &ctx) override;
    const char *getStateName() const override;
    const char *getBehaviorType() const override { return "Default"; }

  private:
    mutable State state = Entering;
    mutable int targetAisleNode = -1;
    mutable std::vector<int> preferredAisleNodes;

    int getNextEdgeToNode(int currentNodeId, int targetNodeId, const StoreGraph &store) const;
    int getNextEdgeToNodeType(int currentNodeId, int targetType, const StoreGraph &store) const;
};

/** Mission behavior: targets specific SKUs, minimal wandering, quick exit after list complete. */
class MissionBehavior : public ICustomerBehavior {
  public:
    enum State { Entering, MissionBrowse, HeadingToCheckout, InQueue, HeadingToExit, Exiting, Done };

    MissionBehavior();
    void onEnterStore(Customer &c, const ICustomerBehaviorContext &ctx) const override;
    Decision decide(Customer &c, const ICustomerBehaviorContext &ctx) override;
    const char *getStateName() const override;
    const char *getBehaviorType() const override { return "Mission"; }

  private:
    mutable State state = Entering;
    mutable std::vector<int> missionSkus;
    mutable size_t missionIndex = 0;
    mutable int missionTargetEdgeIdx = -1;
    mutable int missionTargetCellIdx = -1;
};

} // namespace priceriot

#endif