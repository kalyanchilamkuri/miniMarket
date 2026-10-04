#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include <iostream>

namespace minimarket {

using Price = int64_t;        // Integer price ticks (e.g. cents: 10000 = $100.00)
using Quantity = int64_t;     // Integer share quantities
using OrderId = uint64_t;     // Monotonic unique order identifier
using Timestamp = uint64_t;   // Simulation step / logical timestamp

enum class Side {
    Buy,
    Sell
};

inline const char* sideToString(Side side) {
    return side == Side::Buy ? "BUY" : "SELL";
}

inline Side oppositeSide(Side side) {
    return side == Side::Buy ? Side::Sell : Side::Buy;
}

enum class OrderType {
    Limit,
    Market
};

inline const char* orderTypeToString(OrderType type) {
    return type == OrderType::Limit ? "LIMIT" : "MARKET";
}

enum class OrderStatus {
    New,
    PartiallyFilled,
    Filled,
    Cancelled,
    Rejected
};

inline const char* orderStatusToString(OrderStatus status) {
    switch (status) {
        case OrderStatus::New: return "NEW";
        case OrderStatus::PartiallyFilled: return "PARTIALLY_FILLED";
        case OrderStatus::Filled: return "FILLED";
        case OrderStatus::Cancelled: return "CANCELLED";
        case OrderStatus::Rejected: return "REJECTED";
        default: return "UNKNOWN";
    }
}

struct Trade {
    uint64_t trade_id;
    OrderId buy_order_id;
    OrderId sell_order_id;
    std::string buy_client_id;
    std::string sell_client_id;
    Price price;             // Executed strictly at resting order's price
    Quantity qty;
    Timestamp timestamp;
};

struct LevelInfo {
    Price price{0};
    Quantity total_qty{0};
    size_t order_count{0};
};

struct BookSnapshot {
    Timestamp timestamp{0};
    std::optional<Price> best_bid;
    std::optional<Quantity> best_bid_qty;
    std::optional<Price> best_ask;
    std::optional<Quantity> best_ask_qty;
    std::optional<double> mid_price;
    std::optional<Price> spread;
};

} // namespace minimarket
