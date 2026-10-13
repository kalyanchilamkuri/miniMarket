#include "minimarket/Simulator.hpp"
#include <filesystem>
#include <iostream>

namespace minimarket {

Simulator::Simulator(SimulationConfig config)
    : config_(std::move(config)),
      strategy_(config_.strategy_config, config_.portfolio_config),
      rng_(config_.seed),
      latent_price_(config_.initial_reference_price) {}

void Simulator::seedInitialBook() {
    current_time_ = 0;
    // Seed 3 bid levels and 3 ask levels around the initial latent price
    for (Price p = 1; p <= 3; ++p) {
        OrderId bid_id = next_order_id_++;
        book_.addLimitOrder(bid_id, "EXT_INIT", Side::Buy, latent_price_ - p, 25, current_time_);
        active_external_orders_.push_back(bid_id);

        OrderId ask_id = next_order_id_++;
        book_.addLimitOrder(ask_id, "EXT_INIT", Side::Sell, latent_price_ + p, 25, current_time_);
        active_external_orders_.push_back(ask_id);
    }
}

void Simulator::handleTrades(const std::vector<Trade>& trades) {
    const std::string& strat_client = config_.strategy_config.client_id;
    for (const auto& trade : trades) {
        all_trades_.push_back(trade);

        if (trade.buy_client_id == strat_client) {
            strategy_.onTradeFill(trade, Side::Buy);
        }
        if (trade.sell_client_id == strat_client) {
            strategy_.onTradeFill(trade, Side::Sell);
        }
    }
}

void Simulator::processStrategyActions() {
    auto actions = strategy_.onMarketUpdate(book_, current_time_, next_order_id_);
    for (const auto& action : actions) {
        if (action.action == StrategyActionType::Cancel) {
            book_.cancelOrder(action.order_id);
            strategy_.onOrderCancelled(action.order_id);
        } else if (action.action == StrategyActionType::Place) {
            auto trades = book_.addLimitOrder(action.order_id,
                                              config_.strategy_config.client_id,
                                              action.side,
                                              action.price,
                                              action.qty,
                                              current_time_);
            handleTrades(trades);
        }
    }
}

void Simulator::takeSnapshot() {
    BookSnapshot snap = book_.getSnapshot(current_time_);

    double best_bid = snap.best_bid.value_or(0);
    double best_ask = snap.best_ask.value_or(0);
    double mid = snap.mid_price.value_or(strategy_.portfolio().lastMarkPrice());
    double spread = snap.spread.value_or(0);

    const auto& port = strategy_.portfolio();

    MarketSnapshotRecord record;
    record.timestamp = current_time_;
    record.best_bid = best_bid;
    record.best_ask = best_ask;
    record.mid_price = mid;
    record.spread = spread;
    record.strategy_position = port.position();
    record.strategy_cash = port.cash();
    record.strategy_equity = port.equity(mid);
    record.strategy_pnl = port.netPnL(mid);
    record.cumulative_fees = port.cumulativeFees();
    record.strategy_fill_ratio = strategy_.fillRatio();

    snapshots_.push_back(record);
}

SimulationStats Simulator::run() {
    book_.clear();
    strategy_.reset();
    strategy_.portfolio().reset(config_.initial_reference_price);
    all_trades_.clear();
    snapshots_.clear();
    active_external_orders_.clear();
    next_order_id_ = 1;
    latent_price_ = config_.initial_reference_price;
    rng_.seed(config_.seed);

    seedInitialBook();
    takeSnapshot();

    std::discrete_distribution<int> price_jump_dist({15, 70, 15}); // -1, 0, +1
    std::discrete_distribution<int> event_dist({55, 30, 15});      // Limit, Market, Cancel
    std::bernoulli_distribution side_dist(0.5);
    std::uniform_int_distribution<Price> offset_dist(1, 4);
    std::uniform_int_distribution<Quantity> limit_qty_dist(5, 20);
    std::uniform_int_distribution<Quantity> market_qty_dist(1, 15);

    for (size_t step = 1; step <= config_.num_events; ++step) {
        current_time_ = step;

        // 1. Latent asset price random walk
        int jump = price_jump_dist(rng_) - 1;
        latent_price_ += jump;

        // 2. Sample external event
        int event_type = event_dist(rng_);

        if (event_type == 0) {
            // External Limit Order
            Side side = side_dist(rng_) ? Side::Buy : Side::Sell;
            Price offset = offset_dist(rng_);
            Price order_price = (side == Side::Buy) ? (latent_price_ - offset) : (latent_price_ + offset);
            Quantity qty = limit_qty_dist(rng_);

            OrderId id = next_order_id_++;
            auto trades = book_.addLimitOrder(id, "EXT_MAKER", side, order_price, qty, current_time_);
            handleTrades(trades);

            if (book_.hasOrder(id)) {
                active_external_orders_.push_back(id);
            }
        } else if (event_type == 1) {
            // External Market Order (Liquidity taker)
            Side side = side_dist(rng_) ? Side::Buy : Side::Sell;
            Quantity qty = market_qty_dist(rng_);

            OrderId id = next_order_id_++;
            auto trades = book_.addMarketOrder(id, "EXT_TAKER", side, qty, current_time_);
            handleTrades(trades);
        } else if (event_type == 2) {
            // External Cancel
            if (!active_external_orders_.empty()) {
                std::uniform_int_distribution<size_t> idx_dist(0, active_external_orders_.size() - 1);
                size_t idx = idx_dist(rng_);
                OrderId id_to_cancel = active_external_orders_[idx];

                book_.cancelOrder(id_to_cancel);

                active_external_orders_[idx] = active_external_orders_.back();
                active_external_orders_.pop_back();
            }
        }

        // 3. Strategy observation & order placement
        processStrategyActions();

        // 4. Periodic snapshot
        if (step % config_.snapshot_interval == 0 || step == config_.num_events) {
            takeSnapshot();
        }
    }

    const auto& port = strategy_.portfolio();
    double mark = book_.midPrice().value_or(port.lastMarkPrice());

    SimulationStats stats;
    stats.total_events = config_.num_events;
    stats.total_trades = all_trades_.size();
    stats.strategy_trades = port.tradeCount();
    stats.strategy_bought_volume = port.totalBoughtVolume();
    stats.strategy_sold_volume = port.totalSoldVolume();
    stats.strategy_final_inventory = port.position();
    stats.strategy_final_cash = port.cash();
    stats.strategy_final_equity = port.equity(mark);
    stats.strategy_final_pnl = port.netPnL(mark);
    stats.strategy_cumulative_fees = port.cumulativeFees();
    stats.strategy_fill_ratio = strategy_.fillRatio();

    return stats;
}

bool Simulator::exportCsv(const std::string& out_dir) const {
    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);

    std::string trades_path = out_dir + "/trades.csv";
    std::string snapshots_path = out_dir + "/snapshots.csv";

    bool ok_trades = CsvWriter::writeTrades(trades_path, all_trades_);
    bool ok_snaps = CsvWriter::writeSnapshots(snapshots_path, snapshots_);

    return ok_trades && ok_snaps;
}

} // namespace minimarket
