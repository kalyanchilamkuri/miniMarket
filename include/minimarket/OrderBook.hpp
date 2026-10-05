#pragma once

#include "minimarket/Types.hpp"
#include "minimarket/Order.hpp"

#include <map>
#include <list>
#include <unordered_map>
#include <vector>
#include <optional>
#include <functional>

namespace minimarket {

struct LimitLevel {
    Price price{0};
    Quantity total_volume{0};
    std::list<Order> orders;
};

class OrderBook {
public:
    OrderBook();

    // Core Matching Engine API
    std::vector<Trade> addOrder(const Order& order);
    std::vector<Trade> addLimitOrder(OrderId id,
                                     std::string client_id,
                                     Side side,
                                     Price price,
                                     Quantity qty,
                                     Timestamp timestamp);
    std::vector<Trade> addMarketOrder(OrderId id,
                                      std::string client_id,
                                      Side side,
                                      Quantity qty,
                                      Timestamp timestamp);
    bool cancelOrder(OrderId id);

    // Book State Queries
    std::optional<Price> bestBid() const;
    std::optional<Price> bestAsk() const;
    std::optional<Quantity> bestBidQty() const;
    std::optional<Quantity> bestAskQty() const;
    std::optional<double> midPrice() const;
    std::optional<Price> spread() const;

    BookSnapshot getSnapshot(Timestamp timestamp) const;
    std::vector<LevelInfo> getBidDepth(size_t max_levels = 5) const;
    std::vector<LevelInfo> getAskDepth(size_t max_levels = 5) const;

    bool hasOrder(OrderId id) const;
    const Order* getOrder(OrderId id) const;
    size_t orderCount() const { return order_map_.size(); }
    Quantity totalBidVolume() const;
    Quantity totalAskVolume() const;

    void clear();

private:
    struct OrderLocation {
        Side side;
        Price price;
        std::list<Order>::iterator it;
    };

    std::vector<Trade> matchLimitBuy(Order incoming);
    std::vector<Trade> matchLimitSell(Order incoming);
    std::vector<Trade> matchMarketBuy(Order incoming);
    std::vector<Trade> matchMarketSell(Order incoming);

    // Bids sorted descending: highest price first
    std::map<Price, LimitLevel, std::greater<Price>> bids_;
    // Asks sorted ascending: lowest price first
    std::map<Price, LimitLevel, std::less<Price>> asks_;

    // Fast O(1) order lookup
    std::unordered_map<OrderId, OrderLocation> order_map_;

    uint64_t next_trade_id_{1};
};

} // namespace minimarket
