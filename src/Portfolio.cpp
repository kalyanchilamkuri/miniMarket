#include "minimarket/Portfolio.hpp"

namespace minimarket {

Portfolio::Portfolio(Config config)
    : config_(config),
      cash_(config.initial_cash) {}

void Portfolio::reset(double initial_mark_price) {
    cash_ = config_.initial_cash;
    position_ = 0;
    cumulative_fees_ = 0.0;
    trade_count_ = 0;
    total_bought_volume_ = 0;
    total_sold_volume_ = 0;
    last_mark_price_ = initial_mark_price;
}

void Portfolio::onTrade(Side side, Price price, Quantity qty) {
    if (qty <= 0) return;

    double gross_amount = static_cast<double>(price) * static_cast<double>(qty);
    double fee = static_cast<double>(qty) * config_.fee_per_share;

    cumulative_fees_ += fee;
    trade_count_++;

    if (side == Side::Buy) {
        cash_ -= (gross_amount + fee);
        position_ += qty;
        total_bought_volume_ += qty;
    } else {
        cash_ += (gross_amount - fee);
        position_ -= qty;
        total_sold_volume_ += qty;
    }

    last_mark_price_ = static_cast<double>(price);
}

double Portfolio::equity(double mark_price) const {
    return cash_ + (static_cast<double>(position_) * mark_price);
}

double Portfolio::netPnL(double mark_price) const {
    return equity(mark_price) - config_.initial_cash;
}

} // namespace minimarket
