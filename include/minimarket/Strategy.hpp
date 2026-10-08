#pragma once

#include "minimarket/Types.hpp"
#include "minimarket/OrderBook.hpp"
#include "minimarket/Portfolio.hpp"

#include <optional>
#include <string>
#include <vector>

namespace minimarket {

struct StrategyConfig {
    std::string client_id{"STRATEGY"};
    Quantity inventory_limit{50};      // Hard maximum absolute inventory |q|
    Quantity quote_size{10};           // Size per quote
    double inventory_skew_gamma{0.2};  // Ticks shifted per unit of inventory
    Price half_spread{3};              // Ticks away from reservation price
};

enum class StrategyActionType {
    Cancel,
    Place
};

struct StrategyAction {
    StrategyActionType action;
    OrderId order_id{0};
    Side side{Side::Buy};
    Price price{0};
    Quantity qty{0};
};

class MarketMakingStrategy {
public:
    explicit MarketMakingStrategy(StrategyConfig config, Portfolio::Config port_config = Portfolio::Config());

    // Resets strategy state
    void reset();

    // Called on new market data snapshot / event step to evaluate and generate actions
    // NOTE: Strictly uses observable public book state (midpoint / best bid / best ask)
    // and internal portfolio. Zero knowledge of future events or latent generator state.
    std::vector<StrategyAction> onMarketUpdate(const OrderBook& book, Timestamp timestamp, OrderId& next_order_id);

    // Notification when one of our orders fills
    void onTradeFill(const Trade& trade, Side side);

    // Notification when our order was cancelled
    void onOrderCancelled(OrderId order_id);

    // Queries
    const Portfolio& portfolio() const { return portfolio_; }
    Portfolio& portfolio() { return portfolio_; }
    const StrategyConfig& config() const { return config_; }

    std::optional<OrderId> activeBidId() const { return active_bid_id_; }
    std::optional<OrderId> activeAskId() const { return active_ask_id_; }
    std::optional<Price> activeBidPrice() const { return active_bid_price_; }
    std::optional<Price> activeAskPrice() const { return active_ask_price_; }

    uint64_t totalOrdersPlaced() const { return total_orders_placed_; }
    Quantity totalQuotedVolume() const { return total_quoted_volume_; }
    Quantity totalFilledVolume() const { return portfolio_.totalBoughtVolume() + portfolio_.totalSoldVolume(); }
    double fillRatio() const;

private:
    StrategyConfig config_;
    Portfolio portfolio_;

    std::optional<OrderId> active_bid_id_;
    std::optional<Price> active_bid_price_;
    std::optional<Quantity> active_bid_qty_;

    std::optional<OrderId> active_ask_id_;
    std::optional<Price> active_ask_price_;
    std::optional<Quantity> active_ask_qty_;

    uint64_t total_orders_placed_{0};
    Quantity total_quoted_volume_{0};
};

} // namespace minimarket
