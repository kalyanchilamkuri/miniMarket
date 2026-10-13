#include "minimarket/OrderBook.hpp"
#include <algorithm>

namespace minimarket {

OrderBook::OrderBook() = default;

void OrderBook::clear() {
    bids_.clear();
    asks_.clear();
    order_map_.clear();
    next_trade_id_ = 1;
}

std::vector<Trade> OrderBook::addOrder(const Order& order) {
    if (order.openQty() <= 0) {
        return {};
    }
    if (order.type() == OrderType::Limit && order.price() <= 0) {
        return {};
    }
    if (order_map_.find(order.id()) != order_map_.end()) {
        // Reject duplicate OrderId
        return {};
    }

    if (order.type() == OrderType::Limit) {
        if (order.side() == Side::Buy) {
            return matchLimitBuy(order);
        } else {
            return matchLimitSell(order);
        }
    } else {
        if (order.side() == Side::Buy) {
            return matchMarketBuy(order);
        } else {
            return matchMarketSell(order);
        }
    }
}

std::vector<Trade> OrderBook::addLimitOrder(OrderId id,
                                             std::string client_id,
                                             Side side,
                                             Price price,
                                             Quantity qty,
                                             Timestamp timestamp) {
    Order order(id, std::move(client_id), side, OrderType::Limit, price, qty, timestamp);
    return addOrder(order);
}

std::vector<Trade> OrderBook::addMarketOrder(OrderId id,
                                              std::string client_id,
                                              Side side,
                                              Quantity qty,
                                              Timestamp timestamp) {
    Order order(id, std::move(client_id), side, OrderType::Market, 0, qty, timestamp);
    return addOrder(order);
}

std::vector<Trade> OrderBook::matchLimitBuy(Order incoming) {
    std::vector<Trade> trades;

    while (incoming.openQty() > 0 && !asks_.empty()) {
        auto best_ask_it = asks_.begin();
        Price best_ask_price = best_ask_it->first;

        // If incoming buy price is lower than lowest ask, cannot match
        if (incoming.price() < best_ask_price) {
            break;
        }

        LimitLevel& level = best_ask_it->second;
        while (incoming.openQty() > 0 && !level.orders.empty()) {
            Order& resting = level.orders.front();

            // Self-Trade Prevention (STP - Cancel Resting policy):
            // Prevent wash trading between orders from the same entity.
            // Cancels the stale resting quote and allows the incoming order to continue matching.
            if (!incoming.clientId().empty() && incoming.clientId() == resting.clientId()) {
                order_map_.erase(resting.id());
                level.total_volume -= resting.openQty();
                level.orders.pop_front();
                continue;
            }

            Quantity match_qty = std::min(incoming.openQty(), resting.openQty());

            // Execution price is the resting order's price
            Trade trade{
                next_trade_id_++,
                incoming.id(),
                resting.id(),
                incoming.clientId(),
                resting.clientId(),
                resting.price(),
                match_qty,
                incoming.timestamp()
            };
            trades.push_back(trade);

            incoming.reduceQty(match_qty);
            resting.reduceQty(match_qty);
            level.total_volume -= match_qty;

            if (resting.isFilled()) {
                order_map_.erase(resting.id());
                level.orders.pop_front();
            }
        }

        if (level.orders.empty()) {
            asks_.erase(best_ask_it);
        }
    }

    // Any remaining quantity rests in the bid book
    if (incoming.openQty() > 0) {
        LimitLevel& level = bids_[incoming.price()];
        level.price = incoming.price();
        level.total_volume += incoming.openQty();
        level.orders.push_back(incoming);
        auto it = std::prev(level.orders.end());
        order_map_[incoming.id()] = {Side::Buy, incoming.price(), it};
    }

    return trades;
}

std::vector<Trade> OrderBook::matchLimitSell(Order incoming) {
    std::vector<Trade> trades;

    while (incoming.openQty() > 0 && !bids_.empty()) {
        auto best_bid_it = bids_.begin();
        Price best_bid_price = best_bid_it->first;

        // If incoming sell price is higher than highest bid, cannot match
        if (incoming.price() > best_bid_price) {
            break;
        }

        LimitLevel& level = best_bid_it->second;
        while (incoming.openQty() > 0 && !level.orders.empty()) {
            Order& resting = level.orders.front();

            // Self-Trade Prevention (STP - Cancel Resting policy):
            // If incoming order would match against resting order from same client,
            // cancel the resting order to prevent wash trading.
            if (!incoming.clientId().empty() && incoming.clientId() == resting.clientId()) {
                order_map_.erase(resting.id());
                level.total_volume -= resting.openQty();
                level.orders.pop_front();
                continue;
            }

            Quantity match_qty = std::min(incoming.openQty(), resting.openQty());

            // Execution price is the resting order's price
            Trade trade{
                next_trade_id_++,
                resting.id(),
                incoming.id(),
                resting.clientId(),
                incoming.clientId(),
                resting.price(),
                match_qty,
                incoming.timestamp()
            };
            trades.push_back(trade);

            incoming.reduceQty(match_qty);
            resting.reduceQty(match_qty);
            level.total_volume -= match_qty;

            if (resting.isFilled()) {
                order_map_.erase(resting.id());
                level.orders.pop_front();
            }
        }

        if (level.orders.empty()) {
            bids_.erase(best_bid_it);
        }
    }

    // Any remaining quantity rests in the ask book
    if (incoming.openQty() > 0) {
        LimitLevel& level = asks_[incoming.price()];
        level.price = incoming.price();
        level.total_volume += incoming.openQty();
        level.orders.push_back(incoming);
        auto it = std::prev(level.orders.end());
        order_map_[incoming.id()] = {Side::Sell, incoming.price(), it};
    }

    return trades;
}

std::vector<Trade> OrderBook::matchMarketBuy(Order incoming) {
    std::vector<Trade> trades;

    while (incoming.openQty() > 0 && !asks_.empty()) {
        auto best_ask_it = asks_.begin();
        LimitLevel& level = best_ask_it->second;

        while (incoming.openQty() > 0 && !level.orders.empty()) {
            Order& resting = level.orders.front();

            // Self-Trade Prevention (STP - Cancel Resting policy)
            if (!incoming.clientId().empty() && incoming.clientId() == resting.clientId()) {
                order_map_.erase(resting.id());
                level.total_volume -= resting.openQty();
                level.orders.pop_front();
                continue;
            }

            Quantity match_qty = std::min(incoming.openQty(), resting.openQty());

            Trade trade{
                next_trade_id_++,
                incoming.id(),
                resting.id(),
                incoming.clientId(),
                resting.clientId(),
                resting.price(),
                match_qty,
                incoming.timestamp()
            };
            trades.push_back(trade);

            incoming.reduceQty(match_qty);
            resting.reduceQty(match_qty);
            level.total_volume -= match_qty;

            if (resting.isFilled()) {
                order_map_.erase(resting.id());
                level.orders.pop_front();
            }
        }

        if (level.orders.empty()) {
            asks_.erase(best_ask_it);
        }
    }

    // Market orders never rest in book (unfilled quantity is cancelled)
    return trades;
}

std::vector<Trade> OrderBook::matchMarketSell(Order incoming) {
    std::vector<Trade> trades;

    while (incoming.openQty() > 0 && !bids_.empty()) {
        auto best_bid_it = bids_.begin();
        LimitLevel& level = best_bid_it->second;

        while (incoming.openQty() > 0 && !level.orders.empty()) {
            Order& resting = level.orders.front();

            // Self-Trade Prevention (STP - Cancel Resting policy)
            if (!incoming.clientId().empty() && incoming.clientId() == resting.clientId()) {
                order_map_.erase(resting.id());
                level.total_volume -= resting.openQty();
                level.orders.pop_front();
                continue;
            }

            Quantity match_qty = std::min(incoming.openQty(), resting.openQty());

            Trade trade{
                next_trade_id_++,
                resting.id(),
                incoming.id(),
                resting.clientId(),
                incoming.clientId(),
                resting.price(),
                match_qty,
                incoming.timestamp()
            };
            trades.push_back(trade);

            incoming.reduceQty(match_qty);
            resting.reduceQty(match_qty);
            level.total_volume -= match_qty;

            if (resting.isFilled()) {
                order_map_.erase(resting.id());
                level.orders.pop_front();
            }
        }

        if (level.orders.empty()) {
            bids_.erase(best_bid_it);
        }
    }

    // Market orders never rest in book (unfilled quantity is cancelled)
    return trades;
}

bool OrderBook::cancelOrder(OrderId id) {
    auto it = order_map_.find(id);
    if (it == order_map_.end()) {
        return false;
    }

    OrderLocation loc = it->second;
    if (loc.side == Side::Buy) {
        auto level_it = bids_.find(loc.price);
        if (level_it != bids_.end()) {
            level_it->second.total_volume -= loc.it->openQty();
            level_it->second.orders.erase(loc.it);
            if (level_it->second.orders.empty()) {
                bids_.erase(level_it);
            }
        }
    } else {
        auto level_it = asks_.find(loc.price);
        if (level_it != asks_.end()) {
            level_it->second.total_volume -= loc.it->openQty();
            level_it->second.orders.erase(loc.it);
            if (level_it->second.orders.empty()) {
                asks_.erase(level_it);
            }
        }
    }

    order_map_.erase(it);
    return true;
}

std::optional<Price> OrderBook::bestBid(const std::string& exclude_client) const {
    if (bids_.empty()) return std::nullopt;
    if (exclude_client.empty()) return bids_.begin()->first;
    for (const auto& [price, level] : bids_) {
        for (const auto& order : level.orders) {
            if (order.clientId() != exclude_client) {
                return price;
            }
        }
    }
    return std::nullopt;
}

std::optional<Price> OrderBook::bestAsk(const std::string& exclude_client) const {
    if (asks_.empty()) return std::nullopt;
    if (exclude_client.empty()) return asks_.begin()->first;
    for (const auto& [price, level] : asks_) {
        for (const auto& order : level.orders) {
            if (order.clientId() != exclude_client) {
                return price;
            }
        }
    }
    return std::nullopt;
}

std::optional<Quantity> OrderBook::bestBidQty(const std::string& exclude_client) const {
    if (bids_.empty()) return std::nullopt;
    if (exclude_client.empty()) return bids_.begin()->second.total_volume;
    for (const auto& [price, level] : bids_) {
        Quantity vol = 0;
        for (const auto& order : level.orders) {
            if (order.clientId() != exclude_client) {
                vol += order.openQty();
            }
        }
        if (vol > 0) return vol;
    }
    return std::nullopt;
}

std::optional<Quantity> OrderBook::bestAskQty(const std::string& exclude_client) const {
    if (asks_.empty()) return std::nullopt;
    if (exclude_client.empty()) return asks_.begin()->second.total_volume;
    for (const auto& [price, level] : asks_) {
        Quantity vol = 0;
        for (const auto& order : level.orders) {
            if (order.clientId() != exclude_client) {
                vol += order.openQty();
            }
        }
        if (vol > 0) return vol;
    }
    return std::nullopt;
}

std::optional<double> OrderBook::midPrice(const std::string& exclude_client) const {
    auto bb = bestBid(exclude_client);
    auto ba = bestAsk(exclude_client);
    if (bb && ba) {
        return (*bb + *ba) / 2.0;
    }
    return std::nullopt;
}

std::optional<Price> OrderBook::spread(const std::string& exclude_client) const {
    auto bb = bestBid(exclude_client);
    auto ba = bestAsk(exclude_client);
    if (bb && ba) {
        return *ba - *bb;
    }
    return std::nullopt;
}

BookSnapshot OrderBook::getSnapshot(Timestamp timestamp) const {
    BookSnapshot snap;
    snap.timestamp = timestamp;
    snap.best_bid = bestBid();
    snap.best_bid_qty = bestBidQty();
    snap.best_ask = bestAsk();
    snap.best_ask_qty = bestAskQty();
    snap.mid_price = midPrice();
    snap.spread = spread();
    return snap;
}

std::vector<LevelInfo> OrderBook::getBidDepth(size_t max_levels) const {
    std::vector<LevelInfo> levels;
    levels.reserve(max_levels);
    size_t count = 0;
    for (const auto& [price, level] : bids_) {
        if (count++ >= max_levels) break;
        levels.push_back({price, level.total_volume, level.orders.size()});
    }
    return levels;
}

std::vector<LevelInfo> OrderBook::getAskDepth(size_t max_levels) const {
    std::vector<LevelInfo> levels;
    levels.reserve(max_levels);
    size_t count = 0;
    for (const auto& [price, level] : asks_) {
        if (count++ >= max_levels) break;
        levels.push_back({price, level.total_volume, level.orders.size()});
    }
    return levels;
}

bool OrderBook::hasOrder(OrderId id) const {
    return order_map_.find(id) != order_map_.end();
}

const Order* OrderBook::getOrder(OrderId id) const {
    auto it = order_map_.find(id);
    if (it == order_map_.end()) {
        return nullptr;
    }
    return &(*(it->second.it));
}

Quantity OrderBook::totalBidVolume() const {
    Quantity total = 0;
    for (const auto& [price, level] : bids_) {
        total += level.total_volume;
    }
    return total;
}

Quantity OrderBook::totalAskVolume() const {
    Quantity total = 0;
    for (const auto& [price, level] : asks_) {
        total += level.total_volume;
    }
    return total;
}

} // namespace minimarket
