#include "minimarket/Strategy.hpp"
#include <cmath>
#include <algorithm>

namespace minimarket {

MarketMakingStrategy::MarketMakingStrategy(StrategyConfig config, Portfolio::Config port_config)
    : config_(std::move(config)),
      portfolio_(port_config) {}

void MarketMakingStrategy::reset() {
    portfolio_.reset();
    active_bid_id_ = std::nullopt;
    active_bid_price_ = std::nullopt;
    active_bid_qty_ = std::nullopt;
    active_ask_id_ = std::nullopt;
    active_ask_price_ = std::nullopt;
    active_ask_qty_ = std::nullopt;
    total_orders_placed_ = 0;
    total_quoted_volume_ = 0;
}

double MarketMakingStrategy::fillRatio() const {
    if (total_quoted_volume_ == 0) return 0.0;
    return static_cast<double>(totalFilledVolume()) / static_cast<double>(total_quoted_volume_);
}

void MarketMakingStrategy::onTradeFill(const Trade& trade, Side side) {
    portfolio_.onTrade(side, trade.price, trade.qty);

    if (side == Side::Buy) {
        if (active_bid_id_ && trade.buy_order_id == *active_bid_id_) {
            if (active_bid_qty_) {
                *active_bid_qty_ -= trade.qty;
                if (*active_bid_qty_ <= 0) {
                    active_bid_id_ = std::nullopt;
                    active_bid_price_ = std::nullopt;
                    active_bid_qty_ = std::nullopt;
                }
            }
        }
    } else {
        if (active_ask_id_ && trade.sell_order_id == *active_ask_id_) {
            if (active_ask_qty_) {
                *active_ask_qty_ -= trade.qty;
                if (*active_ask_qty_ <= 0) {
                    active_ask_id_ = std::nullopt;
                    active_ask_price_ = std::nullopt;
                    active_ask_qty_ = std::nullopt;
                }
            }
        }
    }
}

void MarketMakingStrategy::onOrderCancelled(OrderId order_id) {
    if (active_bid_id_ && *active_bid_id_ == order_id) {
        active_bid_id_ = std::nullopt;
        active_bid_price_ = std::nullopt;
        active_bid_qty_ = std::nullopt;
    }
    if (active_ask_id_ && *active_ask_id_ == order_id) {
        active_ask_id_ = std::nullopt;
        active_ask_price_ = std::nullopt;
        active_ask_qty_ = std::nullopt;
    }
}

std::vector<StrategyAction> MarketMakingStrategy::onMarketUpdate(const OrderBook& book,
                                                                Timestamp /*timestamp*/,
                                                                OrderId& next_order_id) {
    std::vector<StrategyAction> actions;

    // Midpoint observation: anchor to external market liquidity (excluding own quotes)
    // to avoid self-referential midpoint bias and quote sticking
    auto opt_mid = book.midPrice(config_.client_id);
    if (!opt_mid.has_value()) {
        opt_mid = book.midPrice();
    }
    if (!opt_mid.has_value()) {
        // No observable midpoint from both sides of the book
        return actions;
    }

    double mid = *opt_mid;
    portfolio_.updateLastMarkPrice(mid);

    Quantity q = portfolio_.position();

    // Simplified linear inventory-skew heuristic (inspired by the Avellaneda-Stoikov reservation price principle):
    // Reservation price shifts down if long (q > 0) to encourage selling,
    // and shifts up if short (q < 0) to encourage buying.
    double reservation_price = mid - (config_.inventory_skew_gamma * static_cast<double>(q));

    Price desired_bid = static_cast<Price>(std::floor(reservation_price - static_cast<double>(config_.half_spread)));
    Price desired_ask = static_cast<Price>(std::ceil(reservation_price + static_cast<double>(config_.half_spread)));

    // Passive liquidity maker rules: avoid crossing external best quotes
    if (auto bb = book.bestBid(config_.client_id)) {
        if (desired_ask <= *bb) {
            desired_ask = *bb + 1;
        }
    }
    if (auto ba = book.bestAsk(config_.client_id)) {
        if (desired_bid >= *ba) {
            desired_bid = *ba - 1;
        }
    }

    // Enforce strictly positive spread (desired_bid < desired_ask) after all clipping
    if (desired_bid >= desired_ask) {
        desired_ask = desired_bid + 1;
    }

    // 1. Evaluate Buy Side (Inventory Limit check)
    Quantity max_buy = std::max<Quantity>(0, config_.inventory_limit - q);
    Quantity buy_size = std::min(config_.quote_size, max_buy);

    if (buy_size <= 0) {
        // Over inventory limit: cancel any active bid
        if (active_bid_id_.has_value()) {
            actions.push_back({StrategyActionType::Cancel, *active_bid_id_, Side::Buy, 0, 0});
            active_bid_id_ = std::nullopt;
            active_bid_price_ = std::nullopt;
            active_bid_qty_ = std::nullopt;
        }
    } else {
        // Check if existing bid is stale
        bool need_new_bid = false;
        if (!active_bid_id_.has_value()) {
            need_new_bid = true;
        } else if (active_bid_price_ != desired_bid || active_bid_qty_ != buy_size) {
            actions.push_back({StrategyActionType::Cancel, *active_bid_id_, Side::Buy, 0, 0});
            active_bid_id_ = std::nullopt;
            active_bid_price_ = std::nullopt;
            active_bid_qty_ = std::nullopt;
            need_new_bid = true;
        }

        if (need_new_bid) {
            OrderId id = next_order_id++;
            actions.push_back({StrategyActionType::Place, id, Side::Buy, desired_bid, buy_size});
            active_bid_id_ = id;
            active_bid_price_ = desired_bid;
            active_bid_qty_ = buy_size;
            total_orders_placed_++;
            total_quoted_volume_ += buy_size;
        }
    }

    // 2. Evaluate Sell Side (Inventory Limit check)
    Quantity max_sell = std::max<Quantity>(0, config_.inventory_limit + q);
    Quantity sell_size = std::min(config_.quote_size, max_sell);

    if (sell_size <= 0) {
        // Under short limit: cancel any active ask
        if (active_ask_id_.has_value()) {
            actions.push_back({StrategyActionType::Cancel, *active_ask_id_, Side::Sell, 0, 0});
            active_ask_id_ = std::nullopt;
            active_ask_price_ = std::nullopt;
            active_ask_qty_ = std::nullopt;
        }
    } else {
        bool need_new_ask = false;
        if (!active_ask_id_.has_value()) {
            need_new_ask = true;
        } else if (active_ask_price_ != desired_ask || active_ask_qty_ != sell_size) {
            actions.push_back({StrategyActionType::Cancel, *active_ask_id_, Side::Sell, 0, 0});
            active_ask_id_ = std::nullopt;
            active_ask_price_ = std::nullopt;
            active_ask_qty_ = std::nullopt;
            need_new_ask = true;
        }

        if (need_new_ask) {
            OrderId id = next_order_id++;
            actions.push_back({StrategyActionType::Place, id, Side::Sell, desired_ask, sell_size});
            active_ask_id_ = id;
            active_ask_price_ = desired_ask;
            active_ask_qty_ = sell_size;
            total_orders_placed_++;
            total_quoted_volume_ += sell_size;
        }
    }

    return actions;
}

} // namespace minimarket
