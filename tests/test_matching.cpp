#include <gtest/gtest.h>
#include "minimarket/OrderBook.hpp"

using namespace minimarket;

class MatchingTest : public ::testing::Test {
protected:
    OrderBook book;
};

// Test: Match strictly at resting order's price
TEST_F(MatchingTest, MatchesAtRestingPrice) {
    // Resting ask at 100
    book.addLimitOrder(1, "Seller", Side::Sell, 100, 10, 1);

    // Incoming buy with willing price 105
    auto trades = book.addLimitOrder(2, "AggressiveBuyer", Side::Buy, 105, 10, 2);

    ASSERT_EQ(trades.size(), 1);
    // Microstructure rule: price must be 100 (resting ask), NOT 105!
    EXPECT_EQ(trades[0].price, 100);
    EXPECT_EQ(trades[0].qty, 10);
    EXPECT_EQ(trades[0].buy_order_id, 2);
    EXPECT_EQ(trades[0].sell_order_id, 1);
}

// Test: Partial fills across resting orders
TEST_F(MatchingTest, PartialFills) {
    // Resting ask of 100 shares at 50
    book.addLimitOrder(1, "Seller", Side::Sell, 50, 100, 1);
    EXPECT_EQ(book.bestAskQty(), 100);

    // Incoming buy for 40 shares
    auto trades1 = book.addLimitOrder(2, "Buyer1", Side::Buy, 50, 40, 2);
    ASSERT_EQ(trades1.size(), 1);
    EXPECT_EQ(trades1[0].qty, 40);
    EXPECT_EQ(trades1[0].price, 50);

    // 60 shares should remain on resting order 1
    EXPECT_EQ(book.bestAskQty(), 60);
    const Order* resting = book.getOrder(1);
    ASSERT_NE(resting, nullptr);
    EXPECT_EQ(resting->openQty(), 60);
    EXPECT_EQ(resting->filledQty(), 40);

    // Second incoming buy for 60 shares completely exhausts order 1
    auto trades2 = book.addLimitOrder(3, "Buyer2", Side::Buy, 50, 60, 3);
    ASSERT_EQ(trades2.size(), 1);
    EXPECT_EQ(trades2[0].qty, 60);

    // Order 1 is completely filled and removed
    EXPECT_FALSE(book.hasOrder(1));
    EXPECT_FALSE(book.bestAsk().has_value());
}

// Test: Multi-level order sweep with resting remainder
TEST_F(MatchingTest, MultiLevelSweepAndRestingRemainder) {
    // Resting asks: 10 shares at 101, 15 shares at 102
    book.addLimitOrder(1, "S1", Side::Sell, 101, 10, 1);
    book.addLimitOrder(2, "S2", Side::Sell, 102, 15, 2);

    // Incoming buy for 30 shares at 102
    // Should match 10 @ 101, 15 @ 102, and remaining 5 should rest as a bid at 102
    auto trades = book.addLimitOrder(3, "BigBuyer", Side::Buy, 102, 30, 3);

    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].price, 101);
    EXPECT_EQ(trades[0].qty, 10);
    EXPECT_EQ(trades[1].price, 102);
    EXPECT_EQ(trades[1].qty, 15);

    // Ask book is now empty
    EXPECT_FALSE(book.bestAsk().has_value());
    // Remainder rests on bid book at 102 with 5 shares
    EXPECT_EQ(book.bestBid(), 102);
    EXPECT_EQ(book.bestBidQty(), 5);
    EXPECT_TRUE(book.hasOrder(3));
}

// Test: Market order sweeps and unfilled part does not rest
TEST_F(MatchingTest, MarketOrderBehavior) {
    book.addLimitOrder(1, "S1", Side::Sell, 200, 10, 1);
    book.addLimitOrder(2, "S2", Side::Sell, 205, 20, 2);

    // Market Buy for 50 shares (only 30 available in book)
    auto trades = book.addMarketOrder(3, "MarketBuyer", Side::Buy, 50, 3);

    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].price, 200);
    EXPECT_EQ(trades[0].qty, 10);
    EXPECT_EQ(trades[1].price, 205);
    EXPECT_EQ(trades[1].qty, 20);

    // Total traded = 30
    // Asks are now empty
    EXPECT_FALSE(book.bestAsk().has_value());
    // Market order unfilled 20 shares must NOT rest in bids
    EXPECT_FALSE(book.bestBid().has_value());
    EXPECT_EQ(book.orderCount(), 0);
    EXPECT_FALSE(book.hasOrder(3));
}

// Test: Filled order cannot be filled again or cancelled
TEST_F(MatchingTest, FilledOrderCannotBeFilledAgain) {
    book.addLimitOrder(1, "Seller", Side::Sell, 100, 10, 1);
    auto trades = book.addLimitOrder(2, "Buyer", Side::Buy, 100, 10, 2);

    EXPECT_EQ(trades.size(), 1);
    EXPECT_FALSE(book.hasOrder(1));
    EXPECT_FALSE(book.hasOrder(2));

    // Trying to cancel filled order returns false
    EXPECT_FALSE(book.cancelOrder(1));
    EXPECT_FALSE(book.cancelOrder(2));
}

// Regression Test: Self-trade prevention (STP - Cancel Resting policy)
TEST_F(MatchingTest, SelfTradePreventionCancelRestingLimitBuy) {
    // Resting ask by STRATEGY at 100 for 10 shares
    book.addLimitOrder(1, "STRATEGY", Side::Sell, 100, 10, 1);
    EXPECT_TRUE(book.hasOrder(1));
    EXPECT_EQ(book.bestAsk(), 100);

    // Incoming buy by STRATEGY at 100 for 10 shares
    auto trades = book.addLimitOrder(2, "STRATEGY", Side::Buy, 100, 10, 2);

    // No trade should occur
    EXPECT_EQ(trades.size(), 0);
    // Resting ask must be cancelled by the STP policy
    EXPECT_FALSE(book.hasOrder(1));
    EXPECT_FALSE(book.bestAsk().has_value());
    // Incoming buy rests in the book
    EXPECT_TRUE(book.hasOrder(2));
    EXPECT_EQ(book.bestBid(), 100);
    EXPECT_EQ(book.bestBidQty(), 10);
}

TEST_F(MatchingTest, SelfTradePreventionCancelRestingLimitSell) {
    // Resting bid by STRATEGY at 100 for 10 shares
    book.addLimitOrder(1, "STRATEGY", Side::Buy, 100, 10, 1);
    EXPECT_TRUE(book.hasOrder(1));
    EXPECT_EQ(book.bestBid(), 100);

    // Incoming sell by STRATEGY at 100 for 10 shares
    auto trades = book.addLimitOrder(2, "STRATEGY", Side::Sell, 100, 10, 2);

    EXPECT_EQ(trades.size(), 0);
    EXPECT_FALSE(book.hasOrder(1));
    EXPECT_FALSE(book.bestBid().has_value());
    EXPECT_TRUE(book.hasOrder(2));
    EXPECT_EQ(book.bestAsk(), 100);
    EXPECT_EQ(book.bestAskQty(), 10);
}

TEST_F(MatchingTest, SelfTradePreventionCancelRestingMarketOrder) {
    // Resting ask by STRATEGY at 100 for 10 shares
    book.addLimitOrder(1, "STRATEGY", Side::Sell, 100, 10, 1);

    // Incoming market buy by STRATEGY for 10 shares
    auto trades = book.addMarketOrder(2, "STRATEGY", Side::Buy, 10, 2);

    EXPECT_EQ(trades.size(), 0);
    EXPECT_FALSE(book.hasOrder(1));
    EXPECT_FALSE(book.bestAsk().has_value());
    EXPECT_FALSE(book.hasOrder(2)); // Market order unexecuted qty cancels
}
