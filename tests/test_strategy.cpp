#include <gtest/gtest.h>
#include "minimarket/Strategy.hpp"
#include "minimarket/OrderBook.hpp"
#include "minimarket/Simulator.hpp"

using namespace minimarket;

class StrategyTest : public ::testing::Test {
protected:
    StrategyConfig strat_config{
        "STRATEGY",
        20,   // inventory limit = 20
        5,    // quote size = 5
        0.5,  // gamma = 0.5 ticks per share
        2     // half spread = 2 ticks
    };
    MarketMakingStrategy strategy{strat_config};
    OrderBook book;
};

// Test: Midpoint quoting when flat (zero inventory)
TEST_F(StrategyTest, QuotingWhenFlat) {
    // Book has bids at 98, asks at 102. Midpoint = 100.
    book.addLimitOrder(1, "EXT", Side::Buy, 98, 10, 1);
    book.addLimitOrder(2, "EXT", Side::Sell, 102, 10, 1);

    OrderId next_id = 100;
    auto actions = strategy.onMarketUpdate(book, 1, next_id);

    // Should place a Buy and a Sell
    ASSERT_EQ(actions.size(), 2);

    // Reservation price = 100 - (0.5 * 0) = 100.
    // Desired bid = 100 - 2 = 98. Desired ask = 100 + 2 = 102.
    // Passive rule clips to best bid/ask bounds: bid <= best_ask - 1 (101), ask >= best_bid + 1 (99).
    EXPECT_EQ(strategy.activeBidPrice(), 98);
    EXPECT_EQ(strategy.activeAskPrice(), 102);
}

// Test: Inventory skew lowers reservation price when long
TEST_F(StrategyTest, InventorySkewWhenLong) {
    book.addLimitOrder(1, "EXT", Side::Buy, 90, 50, 1);
    book.addLimitOrder(2, "EXT", Side::Sell, 110, 50, 1);
    // Midpoint = 100

    // Simulate receiving fills to make inventory +10 (long)
    Trade fill{1, 100, 1, "STRATEGY", "EXT", 100, 10, 1};
    strategy.onTradeFill(fill, Side::Buy);
    EXPECT_EQ(strategy.portfolio().position(), 10);

    OrderId next_id = 200;
    auto actions = strategy.onMarketUpdate(book, 2, next_id);

    // R(q) = 100 - (0.5 * 10) = 95.
    // Desired bid = 95 - 2 = 93.
    // Desired ask = 95 + 2 = 97.
    // Notice quotes shifted down by 5 ticks to encourage offloading inventory!
    EXPECT_EQ(strategy.activeBidPrice(), 93);
    EXPECT_EQ(strategy.activeAskPrice(), 97);
}

// Test: Inventory skew raises reservation price when short
TEST_F(StrategyTest, InventorySkewWhenShort) {
    book.addLimitOrder(1, "EXT", Side::Buy, 90, 50, 1);
    book.addLimitOrder(2, "EXT", Side::Sell, 110, 50, 1);
    // Midpoint = 100

    // Simulate receiving fills to make inventory -10 (short)
    Trade fill{1, 1, 200, "EXT", "STRATEGY", 100, 10, 1};
    strategy.onTradeFill(fill, Side::Sell);
    EXPECT_EQ(strategy.portfolio().position(), -10);

    OrderId next_id = 300;
    auto actions = strategy.onMarketUpdate(book, 2, next_id);

    // R(q) = 100 - (0.5 * -10) = 105.
    // Desired bid = 105 - 2 = 103.
    // Desired ask = 105 + 2 = 107.
    // Notice quotes shifted up by 5 ticks to encourage buying back to cover!
    EXPECT_EQ(strategy.activeBidPrice(), 103);
    EXPECT_EQ(strategy.activeAskPrice(), 107);
}

// Test: Hard inventory limits prevent quotes beyond limit
TEST_F(StrategyTest, EnforcesInventoryLimitHardCap) {
    book.addLimitOrder(1, "EXT", Side::Buy, 90, 50, 1);
    book.addLimitOrder(2, "EXT", Side::Sell, 110, 50, 1);

    // Fill strategy up to exactly the inventory limit (20 shares)
    Trade fill{1, 100, 1, "STRATEGY", "EXT", 100, 20, 1};
    strategy.onTradeFill(fill, Side::Buy);
    EXPECT_EQ(strategy.portfolio().position(), 20);

    OrderId next_id = 400;
    auto actions = strategy.onMarketUpdate(book, 3, next_id);

    // Must NOT quote a Buy order since position is already at limit!
    EXPECT_FALSE(strategy.activeBidId().has_value());
    // Must quote an Ask order to liquidate
    EXPECT_TRUE(strategy.activeAskId().has_value());
}

// Regression Test: Strict positive spread (desired_bid < desired_ask) after clipping
TEST_F(StrategyTest, PostClippingSpreadPreserved) {
    // Tight 1-tick external spread: bid 100, ask 101
    book.addLimitOrder(1, "EXT", Side::Buy, 100, 10, 1);
    book.addLimitOrder(2, "EXT", Side::Sell, 101, 10, 1);

    OrderId next_id = 500;
    auto actions = strategy.onMarketUpdate(book, 1, next_id);

    ASSERT_TRUE(strategy.activeBidPrice().has_value());
    ASSERT_TRUE(strategy.activeAskPrice().has_value());
    EXPECT_LT(*strategy.activeBidPrice(), *strategy.activeAskPrice());
}

// Regression Test: Own quotes are excluded from midpoint observation
TEST_F(StrategyTest, ExcludeOwnQuotesFromMidpointObservation) {
    // External market is wide: bid 90, ask 110 (external mid = 100)
    book.addLimitOrder(1, "EXT", Side::Buy, 90, 10, 1);
    book.addLimitOrder(2, "EXT", Side::Sell, 110, 10, 1);

    // Strategy places passive bid at 96 and ask at 104
    book.addLimitOrder(10, "STRATEGY", Side::Buy, 96, 5, 2);
    book.addLimitOrder(11, "STRATEGY", Side::Sell, 104, 5, 2);

    OrderId next_id = 600;
    // Calling onMarketUpdate should base reservation price on external mid 100, NOT self-polluted mid
    auto actions = strategy.onMarketUpdate(book, 3, next_id);

    // Strategy reservation price when flat must be 100
    // desired_bid = 100 - 2 = 98, desired_ask = 100 + 2 = 102
    EXPECT_EQ(strategy.activeBidPrice(), 98);
    EXPECT_EQ(strategy.activeAskPrice(), 102);
}

// Test: Simulator reproducibility with identical seed
TEST(SimulationReproducibilityTest, SameSeedProducesIdenticalResults) {
    SimulationConfig config1;
    config1.seed = 12345;
    config1.num_events = 500;

    SimulationConfig config2 = config1;

    Simulator sim1(config1);
    auto stats1 = sim1.run();

    Simulator sim2(config2);
    auto stats2 = sim2.run();

    EXPECT_EQ(stats1.total_trades, stats2.total_trades);
    EXPECT_EQ(stats1.strategy_trades, stats2.strategy_trades);
    EXPECT_EQ(stats1.strategy_final_inventory, stats2.strategy_final_inventory);
    EXPECT_DOUBLE_EQ(stats1.strategy_final_pnl, stats2.strategy_final_pnl);
    EXPECT_DOUBLE_EQ(stats1.strategy_cumulative_fees, stats2.strategy_cumulative_fees);

    // Bitwise check on all trades
    ASSERT_EQ(sim1.allTrades().size(), sim2.allTrades().size());
    for (size_t i = 0; i < sim1.allTrades().size(); ++i) {
        const auto& t1 = sim1.allTrades()[i];
        const auto& t2 = sim2.allTrades()[i];
        EXPECT_EQ(t1.price, t2.price);
        EXPECT_EQ(t1.qty, t2.qty);
        EXPECT_EQ(t1.buy_order_id, t2.buy_order_id);
        EXPECT_EQ(t1.sell_order_id, t2.sell_order_id);
    }
}

// Regression Test: Midpoint fallback when external quotes disappear
TEST(StrategyMidpointFallbackTest, FallbackToLastValidExternalMidOrRefrain) {
    StrategyConfig strat_config{"STRATEGY", 20, 5, 0.5, 2};
    MarketMakingStrategy strategy{strat_config};
    OrderBook book;
    OrderId next_id = 1;

    // 1. Initial empty book: No external midpoint ever seen -> Refrain from quoting
    auto actions = strategy.onMarketUpdate(book, 1, next_id);
    EXPECT_TRUE(actions.empty());
    EXPECT_FALSE(strategy.activeBidId().has_value());
    EXPECT_FALSE(strategy.activeAskId().has_value());
    EXPECT_FALSE(strategy.lastValidExternalMid().has_value());

    // 2. Add external two-sided quotes: bid 98, ask 102 (mid = 100)
    book.addLimitOrder(next_id++, "EXT", Side::Buy, 98, 10, 2);
    book.addLimitOrder(next_id++, "EXT", Side::Sell, 102, 10, 2);

    actions = strategy.onMarketUpdate(book, 2, next_id);
    EXPECT_EQ(actions.size(), 2);
    ASSERT_TRUE(strategy.lastValidExternalMid().has_value());
    EXPECT_DOUBLE_EQ(*strategy.lastValidExternalMid(), 100.0);
    EXPECT_EQ(strategy.activeBidPrice(), 98);
    EXPECT_EQ(strategy.activeAskPrice(), 102);

    // 3. External ask disappears (one-sided book: only bid 98)
    book.cancelOrder(2); // cancel external ask
    EXPECT_FALSE(book.bestAsk().has_value());
    EXPECT_FALSE(book.midPrice("STRATEGY").has_value());

    // Strategy must fall back to last_valid_external_mid_ (100.0)
    actions = strategy.onMarketUpdate(book, 3, next_id);
    EXPECT_DOUBLE_EQ(*strategy.lastValidExternalMid(), 100.0);
    EXPECT_EQ(strategy.activeBidPrice(), 98);
    EXPECT_EQ(strategy.activeAskPrice(), 102);

    // 4. If strategy is reset and only its own quotes remain in book, it must NOT use own quotes
    strategy.reset();
    book.clear();
    // Add only STRATEGY quotes to book
    book.addLimitOrder(next_id++, "STRATEGY", Side::Buy, 98, 10, 4);
    book.addLimitOrder(next_id++, "STRATEGY", Side::Sell, 102, 10, 4);
    EXPECT_TRUE(book.midPrice().has_value()); // book has quotes
    EXPECT_FALSE(book.midPrice("STRATEGY").has_value()); // but none external!

    actions = strategy.onMarketUpdate(book, 5, next_id);
    EXPECT_TRUE(actions.empty()); // Must refrain from quoting!
    EXPECT_FALSE(strategy.lastValidExternalMid().has_value());
}

// Regression Test: Simultaneous quote replacement & STP preventing timestamp 1264 self-trade
TEST(SelfTradeRegressionTest, SimultaneousQuoteReplacementNoSelfTrade) {
    OrderBook book;
    StrategyConfig strat_config{"STRATEGY", 50, 10, 0.2, 3};
    MarketMakingStrategy strategy{strat_config};

    // External market starts at bid 9970, ask 9980 (mid = 9975)
    book.addLimitOrder(1, "EXT", Side::Buy, 9970, 20, 1);
    book.addLimitOrder(2, "EXT", Side::Sell, 9980, 20, 1);

    OrderId next_id = 100;
    auto actions = strategy.onMarketUpdate(book, 1, next_id);
    for (const auto& a : actions) {
        if (a.action == StrategyActionType::Place) {
            book.addLimitOrder(a.order_id, strat_config.client_id, a.side, a.price, a.qty, 1);
        }
    }
    // Strategy resting bid is 9972, resting ask is 9978
    EXPECT_TRUE(strategy.activeAskId().has_value());
    OrderId old_ask_id = *strategy.activeAskId();
    Price old_ask_price = *strategy.activeAskPrice();
    EXPECT_EQ(old_ask_price, 9978);
    EXPECT_TRUE(book.hasOrder(old_ask_id));

    // Sudden external market jump up to bid 9985, ask 9995 (mid = 9990)
    book.cancelOrder(1);
    book.cancelOrder(2);
    book.addLimitOrder(3, "EXT", Side::Buy, 9985, 20, 2);
    book.addLimitOrder(4, "EXT", Side::Sell, 9995, 20, 2);

    // Strategy updates: new reservation price moves up, new desired bid moves up to ~9987.
    // If actions were processed without canceling old ask first, new bid (9987) would cross old ask (9978)!
    actions = strategy.onMarketUpdate(book, 2, next_id);

    std::vector<Trade> trades_executed;

    // Two-pass execution: Pass 1 (cancels) then Pass 2 (places)
    for (const auto& a : actions) {
        if (a.action == StrategyActionType::Cancel) {
            book.cancelOrder(a.order_id);
            strategy.onOrderCancelled(a.order_id);
        }
    }
    for (const auto& a : actions) {
        if (a.action == StrategyActionType::Place) {
            auto tr = book.addLimitOrder(a.order_id, strat_config.client_id, a.side, a.price, a.qty, 2);
            trades_executed.insert(trades_executed.end(), tr.begin(), tr.end());
        }
    }

    // Verify ZERO trades occurred against self
    EXPECT_EQ(trades_executed.size(), 0);
    for (const auto& t : trades_executed) {
        EXPECT_FALSE(t.buy_client_id == "STRATEGY" && t.sell_client_id == "STRATEGY");
    }
}

// Regression Test: 5000-event Seed-42 simulation has ZERO self-trades and obeys inventory limits
TEST(SimulationSeed42RegressionTest, ZeroSelfTradesAndInventoryAdherence) {
    SimulationConfig config;
    config.seed = 42;
    config.num_events = 5000;
    config.strategy_config.inventory_limit = 50;

    Simulator sim(config);
    auto stats = sim.run();

    // Verify absolutely zero wash trades occurred in the entire simulation
    size_t self_trade_count = 0;
    for (const auto& t : sim.allTrades()) {
        if (t.buy_client_id == "STRATEGY" && t.sell_client_id == "STRATEGY") {
            self_trade_count++;
        }
    }
    EXPECT_EQ(self_trade_count, 0) << "Found " << self_trade_count << " self-trades!";

    // Verify inventory limit was never breached at any event step
    EXPECT_LE(stats.strategy_max_abs_inventory, config.strategy_config.inventory_limit);
}
