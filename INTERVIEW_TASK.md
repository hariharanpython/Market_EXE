# Interview Task: FIX 4.4 Market Data System in C++
# Reference : https://github.com/quickfix/quickfix
## Overview

Your task is to build a two-process market data system using the **FIX 4.4 protocol** and **QuickFIX C++ library**.

The system consists of:
1. **A Market Data Simulator (Server)** — acts like a stock exchange. It accepts FIX connections, manages subscriptions, and streams live price data.
2. **A Market Data Client** — connects to the server, subscribes to symbols, receives price ticks, and aggregates them into OHLC (Open, High, Low, Close) bars saved as CSV files.
3. **A Frontend Dashboard** — displays live Bid and Ask prices by receiving data from the Market Data Client.

The server and client run simultaneously and communicate over a FIX 4.4 socket session, while the client forwards price data to the frontend.

---

## Prerequisites

### Required Knowledge
- C++14 or later
- FIX Protocol 4.4 concepts (sessions, message types, repeating groups)
- Socket-based client/server programming

### Tools & Libraries to Install
| Tool | Purpose |
|------|---------|
| **CMake** ≥ 3.10 | Build system |
| **vcpkg** | C++ package manager |
| **QuickFIX** | FIX engine library |
| **Visual Studio 2019+** (Windows) or **GCC/Clang** (Linux) | C++ compiler |

### Installing QuickFIX via vcpkg
```bash
# Clone vcpkg (if not already installed)
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
bootstrap-vcpkg.bat        # Windows
./bootstrap-vcpkg.sh       # Linux/macOS

# Install QuickFIX
./vcpkg install quickfix
./vcpkg integrate install
```

---

## Task Requirements

### Part 1 — Market Data Simulator (Server)

Create a file called `market_data_simulator.cpp` that implements a **FIX Acceptor** (server).

**Functional Requirements:**
- Use `FIX::SocketAcceptor` to accept incoming FIX connections.
- Maintain a map of symbol → current price for at least 3 forex pairs: `EURUSD`, `GBPUSD`, `USDJPY`.
- Handle incoming **MarketDataRequest (V)** messages. For each requested symbol:
  - Register a subscription mapping the symbol to the requesting session.
  - Immediately send a **MarketDataSnapshotFullRefresh (W)** with the current Bid, Offer, and Trade prices.
- Run a background thread (`priceUpdateLoop`) that:
  - Wakes up every **100 milliseconds**.
  - Applies a small random price walk to each subscribed symbol.
  - Sends a **MarketDataIncrementalRefresh (X)** message to all subscribers.
- Use `server.cfg` for configuration (see Config Files section below).

**FIX Message Details:**
- **MarketDataSnapshotFullRefresh (W):** Include 3 `NoMDEntries` repeating groups:
  - Bid (`MDEntryType = '0'`): price = current_price - 0.0002
  - Offer (`MDEntryType = '1'`): price = current_price + 0.0002
  - Trade (`MDEntryType = '2'`): price = current_price
- **MarketDataIncrementalRefresh (X):** Include 1 entry per symbol:
  - `MDUpdateAction = NEW`
  - `MDEntryType = Trade`
  - Random volume between 10,000 and 100,000

---

### Part 2 — Market Data Client

Create a file called `main.cpp` that implements a **FIX Initiator** (client).

**Functional Requirements:**
- Use `FIX::SocketInitiator` to connect to the simulator.
- On logon, subscribe to market data for: `EURUSD`, `GBPUSD`, `USDJPY`.
- Handle **MarketDataSnapshotFullRefresh (W)** messages — extract Trade price and volume.
- Handle **MarketDataIncrementalRefresh (X)** messages — extract Trade price and volume.
- For each incoming price tick, pass it to an **OHLC Aggregator** (see Part 3).
- Print a status update every 60 seconds showing the current OHLC state for all symbols.
- Handle `SIGINT` / `SIGTERM` gracefully: flush all OHLC data to disk before exiting.
- Use `client.cfg` for configuration (see Config Files section below).

**MarketDataRequest to send:**
```
MDReqID         = "MD_<SYMBOL>"
SubscriptionRequestType = '1'  (Snapshot + Updates)
MarketDepth     = 1            (Top of book)
MDUpdateType    = 1            (Incremental refresh)
NoMDEntryTypes  = [Bid, Offer, Trade]
NoRelatedSym    = [<SYMBOL>]
```

---

### Part 3 — OHLC Bar Aggregator

Create a header file `OHLCBarAggregator.h` implementing the `OHLCBarAggregator` class.

**Functional Requirements:**
- Track **11 timeframes simultaneously** for each symbol:
  -  `30s`, `1m`, `5m`, `1h`
- For each incoming price tick (`onPrice(double price, long volume)`):
  - Determine which bar period it belongs to by rounding down the current time.
  - If the current bar has expired (new time bucket started), **close** the old bar and **save it to a CSV file**, then start a new bar.
  - Update the current bar: set Open (first tick), update High/Low, set Close (last tick), accumulate Volume and TickCount.
- Save completed bars to CSV files inside `./ohlc_data/` directory:
  - Filename format: `<SYMBOL>_<TIMEFRAME>.csv` (e.g., `EURUSD_1m.csv`)
  - CSV columns: `Timestamp,Open,High,Low,Close,Volume,TickCount`
  - Timestamp = Unix epoch in seconds
- Implement `flushAll()` to save all open (incomplete) bars to disk on shutdown.
- Implement `printCurrentState()` to print OHLC state for all timeframes to stdout.

**Required Data Structures:**
```cpp
struct OHLCBar {
    std::chrono::system_clock::time_point timestamp;
    double open, high, low, close;
    long volume;
    int tick_count;
    void update(double price, long vol = 0);
    bool isEmpty() const;
};

enum class Timeframe {
    SEC_30 = 30,
    MIN_1 = 60, MIN_5 = 300,
    HOUR_1 = 3600, 
};
```

---

### Part 4 — Frontend Data Display

Create a frontend application to visualize the live market data.

**Functional Requirements:**
- The C++ Market Data Client must forward the incoming **Bid** and **Ask** (Offer) prices to the frontend in real-time.
- You can choose the technology stack for the frontend:
  - A web-based dashboard (HTML/CSS/JS) connecting via WebSockets or a local HTTP API.
- The UI must display the **Symbol**, **Bid Price**, and **Ask Price** updating dynamically as market data arrives.

---

## Expected Output

**Server terminal:**
```
Starting FIX Market Data Simulator...
Session created: FIX.4.4:SERVER1->CLIENT1
Logon: FIX.4.4:SERVER1->CLIENT1
Received Market Data Request
Subscribed: EURUSD
Sent snapshot for EURUSD @ 1.08500
Update: EURUSD = 1.08512
...
```

**Client terminal:**
```
Starting FIX Market Data Client...
Session created: FIX.4.4:CLIENT1->SERVER1
Logon: FIX.4.4:CLIENT1->SERVER1
Subscribing to market data...
Subscribed to market data for: EURUSD
Created OHLC aggregator for symbol: EURUSD
Trade: EURUSD Price=1.08500 Volume=50000
...
```

**Output files** in `./ohlc_data/`:
```
EURUSD_1m.csv
EURUSD_5m.csv
GBPUSD_1m.csv
...
```

Each CSV will look like:
```
Timestamp,Open,High,Low,Close,Volume,TickCount
1708598400,1.08500,1.08530,1.08480,1.08510,350000,42
```


---

## Deliverables files example

Submit the following files:
- [ ] `market_data_simulator.cpp` — FIX server / simulator
- [ ] `main.cpp` — FIX client entry point
- [ ] `FIXMarketDataApp.h` — Client-side FIX application class
- [ ] `OHLCBarAggregator.h` — OHLC bar aggregation logic
- [ ] `CMakeLists.txt` — Build configuration
- [ ] `server.cfg` — Server FIX session config
- [ ] `client.cfg` — Client FIX session config
- [ ] Frontend source files (HTML/JS, Python, or C++ GUI files)
- [ ] (Optional) `run_simulator.bat` / `run_client.bat` — Helper scripts for Windows

---


