#include <gtest/gtest.h>
#include "minimarket/OrderBook.hpp"

using namespace minimarket;

class OrderBookTest : public ::testing::Test {
protected:
    OrderBook book;
};

// Test 1: Price priority for Buy side (highest bid executed first)
TEST_F(OrderBookTest, PricePriorityBuy) {
    // Add bids at 98, 102, 100
    book.addLimitOrder(1, "A", Side::Buy, 98, 10, 1);
    book.addLimitOrder(2, "B", Side::Buy, 102, 10, 2);
    book.addLimitOrder(3, "C", Side::Buy, 100, 10, 3);

    EXPECT_EQ(book.bestBid(), 102);

    // Incoming Sell for 10 shares at price 95 (crosses all bids)
    auto trades = book.addLimitOrder(4, "Seller", Side::Sell, 95, 10, 4);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].buy_order_id, 2); // Matched best bid at 102 first
    EXPECT_EQ(trades[0].price, 102);      // Resting order price
    EXPECT_EQ(trades[0].qty, 10);

    // Best bid is now 100
    EXPECT_EQ(book.bestBid(), 100);
}

// Test 2: Price priority for Sell side (lowest ask executed first)
TEST_F(OrderBookTest, PricePrioritySell) {
    // Add asks at 105, 101, 103
    book.addLimitOrder(1, "A", Side::Sell, 105, 10, 1);
    book.addLimitOrder(2, "B", Side::Sell, 101, 10, 2);
    book.addLimitOrder(3, "C", Side::Sell, 103, 10, 3);

    EXPECT_EQ(book.bestAsk(), 101);

    // Incoming Buy for 10 shares at 110 (crosses all asks)
    auto trades = book.addLimitOrder(4, "Buyer", Side::Buy, 110, 10, 4);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].sell_order_id, 2); // Matched best ask at 101 first
    EXPECT_EQ(trades[0].price, 101);      // Resting order price
    EXPECT_EQ(trades[0].qty, 10);

    // Best ask is now 103
    EXPECT_EQ(book.bestAsk(), 103);
}

// Test 3: FIFO / Time priority at identical price
TEST_F(OrderBookTest, TimePriorityFIFO) {
    // Two bids at the exact same price 100, order 1 before order 2
    book.addLimitOrder(1, "First", Side::Buy, 100, 10, 10);
    book.addLimitOrder(2, "Second", Side::Buy, 100, 10, 20);

    EXPECT_EQ(book.bestBid(), 100);
    EXPECT_EQ(book.bestBidQty(), 20);

    // Incoming sell for 10 shares
    auto trades = book.addLimitOrder(3, "Seller", Side::Sell, 100, 10, 30);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].buy_order_id, 1); // FIFO: older order 1 matched first!
    EXPECT_EQ(trades[0].qty, 10);

    // Order 2 still resting
    EXPECT_EQ(book.bestBidQty(), 10);
    EXPECT_TRUE(book.hasOrder(2));
    EXPECT_FALSE(book.hasOrder(1));
}

// Test 4: Cancellations and order lookup
TEST_F(OrderBookTest, CancellationRemovesOrder) {
    book.addLimitOrder(1, "Trader1", Side::Buy, 100, 15, 1);
    book.addLimitOrder(2, "Trader2", Side::Buy, 100, 10, 2);

    EXPECT_EQ(book.bestBidQty(), 25);
    EXPECT_TRUE(book.hasOrder(1));

    // Cancel order 1
    bool cancelled = book.cancelOrder(1);
    EXPECT_TRUE(cancelled);
    EXPECT_FALSE(book.hasOrder(1));
    EXPECT_EQ(book.bestBidQty(), 10);

    // Attempting to cancel already cancelled order returns false
    EXPECT_FALSE(book.cancelOrder(1));

    // Cancel order 2 cleans up level
    EXPECT_TRUE(book.cancelOrder(2));
    EXPECT_FALSE(book.bestBid().has_value());
    EXPECT_EQ(book.totalBidVolume(), 0);
}

// Test 5: Empty book safe handling
TEST_F(OrderBookTest, EmptyBookSafeHandling) {
    EXPECT_FALSE(book.bestBid().has_value());
    EXPECT_FALSE(book.bestAsk().has_value());
    EXPECT_FALSE(book.midPrice().has_value());
    EXPECT_FALSE(book.spread().has_value());

    // Market order on empty book produces no trades and does not crash or rest
    auto trades = book.addMarketOrder(1, "Buyer", Side::Buy, 50, 1);
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.orderCount(), 0);
    EXPECT_FALSE(book.hasOrder(1));

    // Cancel non-existent order
    EXPECT_FALSE(book.cancelOrder(999));
}
