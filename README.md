# MiniMarket — Order Book and Trading Strategy Simulator

A clean, modern C++17 limit order book and quantitative market-making strategy simulator with portfolio accounting, reproducible synthetic order flow, and Python analysis.

Developed as a quantitative trading internship portfolio project to demonstrate core competencies in:
- **C++ Software Engineering & Architecture** (C++17, CMake, GoogleTest, RAII, modular static library).
- **Data Structures & Algorithms** (Ordered maps, doubly linked FIFO queues, $O(1)$ order cancellation).
- **Market Microstructure & Order Matching** (Strict price-time priority, integer price ticks, resting-price execution).
- **Quantitative Strategy Design** (Inventory-aware market making inspired by Avellaneda–Stoikov).
- **Portfolio Accounting & Risk Management** (Mark-to-market equity, transaction fee tracking, peak-to-trough drawdown).

---

## Architecture Overview

```
miniMarket/
├── CMakeLists.txt                # Build configuration (C++17, GoogleTest, static runtime)
├── README.md                     # Technical documentation & project guide
├── include/
│   └── minimarket/
│       ├── Types.hpp             # Type definitions (Price, Quantity, OrderId, Side, Trade)
│       ├── Order.hpp             # Limit and market order representation
│       ├── OrderBook.hpp         # Double-sided limit order book & matching engine
│       ├── Portfolio.hpp         # Cash, inventory, fee, and mark-to-market P&L accounting
│       ├── Strategy.hpp          # Inventory-aware market making strategy
│       ├── Simulator.hpp         # Discrete-event synthetic order flow simulator
│       └── CsvWriter.hpp         # CSV export utility for trades and snapshots
├── src/
│   ├── OrderBook.cpp             # Order book matching engine implementation
│   ├── Portfolio.cpp             # Portfolio state machine & accounting implementation
│   ├── Strategy.cpp              # Reservation price calculation & quoting logic
│   ├── Simulator.cpp             # Event generation, latent asset drift, and runner
│   └── main.cpp                  # CLI entry point with configurable flags
├── tests/
│   ├── test_order_book.cpp       # Unit tests for price priority, FIFO, cancels, empty book
│   ├── test_matching.cpp         # Unit tests for partial fills, sweeps, resting price match
│   ├── test_portfolio.cpp        # Unit tests for cash, positions, fees, and mark-to-market
│   └── test_strategy.cpp         # Unit tests for inventory skew, hard caps, reproducibility
├── python/
│   └── analyze.py                # Quantitative analysis script (Pandas & Matplotlib)
└── results/                      # Output directory for CSV datasets and generated plots
    ├── trades.csv                # Execution tape
    ├── snapshots.csv             # Periodic market & portfolio state snapshots
    ├── equity_curve.png          # Mark-to-market equity & drawdown chart
    ├── inventory.png             # Inventory tracking & limit boundaries chart
    └── market_microstructure.png # Bid-ask spread & execution points chart
```

---

## Data Structures & Algorithmic Complexity

The limit order book maintains bids and asks in two distinct order books with price-time priority:

| Component | Data Structure | Rationale | Time Complexity |
|---|---|---|---|
| **Bid Price Levels** | `std::map<Price, LimitLevel, std::greater<Price>>` | Ordered descending so `bids_.begin()` is immediately the highest (best) bid. | $O(\log L)$ insert/delete level, $O(1)$ best bid |
| **Ask Price Levels** | `std::map<Price, LimitLevel, std::less<Price>>` | Ordered ascending so `asks_.begin()` is immediately the lowest (best) ask. | $O(\log L)$ insert/delete level, $O(1)$ best ask |
| **Level FIFO Queue** | `std::list<Order>` | Doubly linked list preserving time priority; node iterators remain valid upon arbitrary insertions and deletions. | $O(1)$ append, $O(1)$ pop/erase |
| **Order Locator** | `std::unordered_map<OrderId, OrderLocation>` | Stores `{Side, Price, std::list<Order>::iterator}` for instant order lookup. | $O(1)$ average lookup |
| **Order Cancellation** | Lookup in `order_map_` + remove from `std::list` + prune empty level | Locates order iterator in $O(1)$, splices from list in $O(1)$, and removes empty level in $O(\log L)$. | $O(\log L)$ where $L$ is active price levels |
| **Order Matching** | Walk opposite book starting at `begin()` | Matches orders sequentially from top of book. | $O(K)$ where $K$ is number of filled resting orders |

---

## Market Microstructure & Matching Invariants

1. **Integer Price Ticks & Quantities**:
   Prices and quantities are strictly represented using `int64_t` ticks (e.g., $1\text{ tick} = 1\text{ cent}$, so $\$100.00 = 10000$). Floating-point numbers are never used in order matching or queue sorting, eliminating rounding discrepancies.
2. **Price-Time Priority (FIFO)**:
   Orders at better prices always execute before orders at worse prices. Orders at the same price execute strictly in FIFO order (the oldest resting order fills first).
3. **Execution at the Resting Order's Price**:
   When an incoming limit or market order crosses the spread, the trade executes at the resting order's price (not the incoming order's price limit).
4. **Order Lifecycle Integrity**:
   Once an order is completely filled, it is removed from the book and can never be filled again or cancelled.
5. **Market Order Behavior**:
   Market orders sweep resting liquidity up to their requested quantity. Any unfilled remainder is immediately cancelled (Immediate-or-Cancel / Fill-and-Kill); market orders never rest on the book.
6. **Information Barrier**:
   The trading strategy operates under a strict firewall. It only receives public order book state (`midPrice()`, `bestBid()`, `bestAsk()`) and its own private portfolio. It has no visibility into the simulator's latent asset price or future event queue.

---

## Quantitative Strategy: Inventory-Aware Market Making

The strategy implements a **simplified linear inventory-skew heuristic** inspired by the reservation price principle from **Avellaneda & Stoikov (2008)**:

### 1. Simplified Linear Reservation Price Heuristic
Given the observable order book midpoint $M = \frac{P_{\text{best bid}} + P_{\text{best ask}}}{2}$ and current inventory position $q$:

$$R(q) = M - \gamma \cdot q$$

where $\gamma$ is a configured inventory risk sensitivity parameter:
- When **long** ($q > 0$): $R(q) < M$, shifting quotes downward to encourage selling and discourage buying.
- When **short** ($q < 0$): $R(q) > M$, shifting quotes upward to encourage buying and discourage shorting.

> **Theoretical Distinction vs. Full Avellaneda–Stoikov (2008)**:
> In the full Avellaneda–Stoikov continuous-time optimal control framework, the reservation price is given by $r(s, q, t) = s - q \gamma \sigma^2 (T - t)$ and optimal spreads $\delta^a, \delta^b$ depend on order flow arrival intensities $\kappa$. MiniMarket intentionally simplifies this into an interview-accessible linear heuristic:
> 1. The composite term $\gamma \sigma^2 (T - t)$ is collapsed into a single static sensitivity parameter $\gamma$.
> 2. Quoting operates over a rolling horizon without finite-horizon terminal liquidation or $T - t$ time decay.
> 3. Quoting spreads use a configurable tick half-spread $s$ with passive liquidity clipping, avoiding numerical intensity fitting.

### 2. Quoting Equations & Midpoint Anchoring
To avoid self-referential feedback loops and quote sticking, the strategy queries the order book **excluding its own client ID** (`book.midPrice("STRATEGY")`, `book.bestBid("STRATEGY")`, `book.bestAsk("STRATEGY")`).

Given a target half-spread $s$:

$$P_{\text{bid}} = \lfloor R(q) - s \rfloor, \quad P_{\text{ask}} = \lceil R(q) + s \rceil$$

Passive maker constraints prevent aggressively crossing the external market, and a strictly positive spread ($P_{\text{ask}} > P_{\text{bid}}$) is guaranteed after all clipping.

### 3. Hard Inventory Limit ($Q_{\max}$)
The strategy enforces a hard inventory ceiling $Q_{\max}$:
- Maximum allowable buy quantity: $\text{buy\_size} = \min(Q_{\text{quote}}, \max(0, Q_{\max} - q))$
- Maximum allowable sell quantity: $\text{sell\_size} = \min(Q_{\text{quote}}, \max(0, Q_{\max} + q))$
- If $q \ge Q_{\max}$, all buy quotes are cancelled and suppressed.
- If $q \le -Q_{\max}$, all sell quotes are cancelled and suppressed.

---

## Portfolio Accounting & P&L Mechanics

1. **Cash Accounting**:
   - Buy fill of quantity $Q$ at price $P$:
     $$\text{Cash} \leftarrow \text{Cash} - (P \cdot Q + \text{Fee})$$
   - Sell fill of quantity $Q$ at price $P$:
     $$\text{Cash} \leftarrow \text{Cash} + (P \cdot Q - \text{Fee})$$
   - Transaction fee: $\text{Fee} = Q \cdot c_{\text{fee}}$ (configurable, default $0.1\text{ cents/share}$).

2. **Position Accounting**:
   - Buy fill: $q \leftarrow q + Q$
   - Sell fill: $q \leftarrow q - Q$

3. **Mark-to-Market Valuation**:
   Given mark price $P_{\text{mark}}$ (the current external order book midpoint, falling back to the last traded price if the book is one-sided):
   $$\text{Equity} = \text{Cash} + q \cdot P_{\text{mark}}$$
   $$\text{Net MTM P\&L} = \text{Equity} - \text{Initial Cash}$$

Transaction fees are deducted directly from cash on each fill, ensuring all fees are fully reflected in net P&L and total equity.

---

## Build and Run Instructions

### Prerequisites
- C++17 compatible compiler (e.g., GCC 9+, Clang 10+, or MSVC 2019+)
- CMake 3.16+
- Python 3.8+ with `pandas` and `matplotlib`

### 1. Build Core and Test Suite
```bash
# Configure build with CMake
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build both the CLI and test binaries
cmake --build build
```

### 2. Run Test Suites
Run all **25 GoogleTest unit tests**:
```bash
./build/minimarket_tests.exe
```
Or via CTest:
```bash
ctest --test-dir build --output-on-failure
```
Run Python analysis tests:
```bash
python -m unittest tests/test_analyze.py
```

### 3. Run Simulation
```bash
# Run 5,000 discrete simulation events with seed 42
./build/minimarket_cli.exe --seed 42 --events 5000 --out results

# Optional CLI flags:
#   --seed <int>          Random seed for reproducibility (default: 42)
#   --events <int>        Number of events to simulate (default: 5000)
#   --out <path>          Output folder for CSV files (default: results)
#   --skew <double>       Inventory skew sensitivity gamma (default: 0.2)
#   --max-inv <int>       Max inventory limit (default: 50)
#   --quote-size <int>    Quote size per order (default: 10)
#   --half-spread <int>   Quote half spread in ticks (default: 3)
```

### 4. Analyze Results and Generate Charts
```bash
python python/analyze.py --dir results
```

---

## Sample Simulation Output (Seed 42, 5000 Events)

```
========================================================
  MiniMarket — Order Book & Strategy Simulator
========================================================
Configuration:
  Random Seed        : 42
  Simulation Events  : 5000
  Output Directory   : results
  Inventory Limit    : 50
  Inventory Skew γ   : 0.2
  Quote Size         : 10
  Half Spread (ticks): 3
--------------------------------------------------------
Running simulation...
Simulation completed successfully!
--------------------------------------------------------
Summary Performance Metrics:
  Total Market Trades: 3760
  Strategy Fills     : 376
  Strategy Fill Ratio: 22.26%
  Bought Volume      : 1306 shares
  Sold Volume        : 1317 shares
  Final Inventory    : -11 shares (limit: 50)
  Cumulative Fees    : $2.62 (262.30 cents)
  Final Net P&L      : $39.27 (3927.20 cents)
  Final Total Equity : $100039.27
--------------------------------------------------------
CSV datasets exported to: results/
  -> results/trades.csv
  -> results/snapshots.csv
========================================================
```

### Quantitative Metrics from Python Analysis
- **Initial Portfolio Equity**: $\$100,000.00$
- **Final Portfolio Equity**: $\$100,039.27$
- **Net Mark-to-Market P&L**: $+\$39.27$ ($+3,927.2\text{ cents}$)
- **Cumulative Fees Deducted**: $\$2.62$
- **Total Trades on Market**: $3,760$
- **Strategy Executions**: $376$ ($195$ buys, $181$ sells)
- **Strategy Volume**: $1,306$ shares bought, $1,317$ shares sold
- **Strategy Fill Ratio**: $22.26\%$ (Volume Filled / Volume Quoted)
- **Max Absolute Inventory**: $50$ shares (strictly bounded at the $50$ share limit)
- **Max Peak-to-Trough Drawdown**: $\$4.52$ ($0.0045\%$)

---

## Unit Test Coverage Summary

### GoogleTest Suite (25 Tests, 100% Passing)
1. `OrderBookTest.PricePriorityBuy` — Verifies highest bid executes first against an incoming sell.
2. `OrderBookTest.PricePrioritySell` — Verifies lowest ask executes first against an incoming buy.
3. `OrderBookTest.TimePriorityFIFO` — Verifies older orders execute first at identical price.
4. `OrderBookTest.CancellationRemovesOrder` — Verifies $O(1)$ queue removal and level pruning.
5. `OrderBookTest.EmptyBookSafeHandling` — Verifies empty order books never crash on market orders.
6. `OrderBookTest.RejectDuplicateOrderId` — Verifies duplicate OrderIds are rejected, preventing map corruption.
7. `OrderBookTest.RejectInvalidLimitPrice` — Verifies limit orders with price $\le 0$ are rejected.
8. `OrderBookTest.ExcludeClientQueries` — Verifies querying book excluding own strategy client ID.
9. `MatchingTest.MatchesAtRestingPrice` — Verifies execution occurs strictly at resting order's price.
10. `MatchingTest.PartialFills` — Verifies partial fills update open quantity and level depth correctly.
11. `MatchingTest.MultiLevelSweepAndRestingRemainder` — Verifies multi-level sweeps and rests remainder.
12. `MatchingTest.MarketOrderBehavior` — Verifies market order sweeps liquidity and drops remainder.
13. `MatchingTest.FilledOrderCannotBeFilledAgain` — Verifies fully filled orders cannot be refilled or cancelled.
14. `PortfolioTest.InitialState` — Verifies clean initial cash, zero position, zero fees.
15. `PortfolioTest.BuyAccounting` — Verifies cash reduction, position addition, and fee deduction.
16. `PortfolioTest.RoundTripRealizedPnL` — Verifies realized P&L on buy-then-sell round trips.
17. `PortfolioTest.ShortPositionAccounting` — Verifies short position mark-to-market mechanics.
18. `PortfolioTest.ResetResetsMarkPrice` — Verifies resetting portfolio restores initial mark price.
19. `StrategyTest.QuotingWhenFlat` — Verifies symmetrical midpoint quoting when inventory is zero.
20. `StrategyTest.InventorySkewWhenLong` — Verifies downward quote skew when inventory is positive.
21. `StrategyTest.InventorySkewWhenShort` — Verifies upward quote skew when inventory is negative.
22. `StrategyTest.EnforcesInventoryLimitHardCap` — Verifies buy quotes are eliminated at $+Q_{\max}$.
23. `StrategyTest.PostClippingSpreadPreserved` — Verifies strictly positive spread ($P_{\text{bid}} < P_{\text{ask}}$) after clipping.
24. `StrategyTest.ExcludeOwnQuotesFromMidpointObservation` — Verifies strategy does not observe its own quotes in midpoint.
25. `SimulationReproducibilityTest.SameSeedProducesIdenticalResults` — Verifies identical seeds yield bitwise identical trades and P&L.

### Python Test Suite (3 Tests, 100% Passing)
1. `TestAnalyzeSimulation.test_standard_metrics` — Verifies P&L, fees, inventory, fill ratio, and peak-to-trough drawdown on known test vectors.
2. `TestAnalyzeSimulation.test_empty_trades_edge_case` — Verifies graceful handling of empty `trades.csv` (0 trades).
3. `TestAnalyzeSimulation.test_zero_volatility_edge_case` — Verifies zero price volatility produces zero drawdown and flat P&L without division-by-zero errors.

---

## Limitations & Engineering Scope

This project is intentionally designed for interview discussions to demonstrate data structures, matching logic, and quantitative finance concepts without unnecessary production overhead. Realistic limitations include:
1. **Single-Instrument In-Memory Engine**: Implemented for a single order book; multi-instrument routing, cross-margining, and FIX protocol adapters are omitted.
2. **Synthetic Discrete Event Flow**: Uses a discrete random walk process rather than full historical L3/ITCH tick data.
3. **Queue Position Assumption**: External cancellation is modeled stochastically rather than tracking granular queue rank dynamics.
4. **Latency Model**: Network transmission delays and matching engine processing latencies are zero in this synchronous discrete-event simulation.
5. **No Commercial Profitability Claim**: The strategy demonstrates inventory mean-reversion and spread capture; it does not claim production alpha or real-world profitability.
