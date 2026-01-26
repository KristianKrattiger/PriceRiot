#ifndef BEHAVIOR_H
#define BEHAVIOR_H

#include <vector>

namespace priceriot {

    // Forward declarations inside namespace
    class StoreGraph;
    class Basket;
    class Customer;

    struct ICustomerBehaviorContext {
        const StoreGraph& store;
        const Basket& basket;
        double dt;
    };

    struct Decision {
        enum DecisionType {
            Move,
            SwitchEdge,
            PickProduct,
            Wait,
            Checkout,
            Despawn
        } type = Move;

        int targetId = -1;
        float duration = 0.0f;
    };

    class ICustomerBehavior {
    public:
        virtual ~ICustomerBehavior() = default;
        virtual void onEnterStore(Customer& c, const ICustomerBehaviorContext& ctx) const {}
        virtual Decision decide(Customer& c, const ICustomerBehaviorContext& ctx) = 0;
    };

    class DefaultBehavior : public ICustomerBehavior {
    public:
        enum State {
            Entering,
            Browsing,
            HeadingToCheckout,
            InQueue,
            HeadingToExit,
            Done
        };

        DefaultBehavior();

        void onEnterStore(Customer& c, const ICustomerBehaviorContext& ctx) const override;
        Decision decide(Customer& c, const ICustomerBehaviorContext& ctx) override;

    private:
        State state = Entering;
        int targetAisleNode = -1;

        int getNextEdgeToNode(int currentNodeId, int targetNodeId, const StoreGraph& store) const;
        int getNextEdgeToNodeType(int currentNodeId, int targetType, const StoreGraph& store) const;
    };

} // namespace priceriot

#endif