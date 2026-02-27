# FIX Market Data System Walkthrough

I have completed the implementation of the FIX Market Data System. This system consists of three main components communicating over FIX 4.4 and WebSockets.

## Implemented Components

### 1. Market Data Simulator (Acceptor)
- **File**: [MarketDataSimulator/market_data_simulator.cpp](file:///c:/ProjectCPP/Anti/Market/MarketDataSimulator/market_data_simulator.cpp)
- **Function**: Acts as the exchange, managing subscriptions and generating random price walks.
- **Key Features**:
    - Handles `MarketDataRequest (V)`.
    - Sends `Snapshot (W)` with 3 entries (Bid, Offer, Trade).
    - Sends `Incremental (X)` every 100ms with random volume (10k-100k).
    - Mutex-protected subscription management.

### 2. Market Data Client (Initiator + OHLC + WS)
- **Files**: 
    - [MarketDataClient/main.cpp](file:///c:/ProjectCPP/Anti/Market/MarketDataClient/main.cpp)
    - [MarketDataClient/FIXMarketDataApp.h](file:///c:/ProjectCPP/Anti/Market/MarketDataClient/FIXMarketDataApp.h)
    - [MarketDataClient/OHLCBarAggregator.h](file:///c:/ProjectCPP/Anti/Market/MarketDataClient/OHLCBarAggregator.h)
- **Function**: Connects to the simulator, aggregates OHLC bars, and streams Bid/Ask prices to the frontend.
- **Key Features**:
    - **OHLC Aggregation**: 30s, 1m, 5m, 1h timeframes.
    - **CSV Storage**: Bars saved to `./ohlc_data/`.
    - **WebSocket Server**: Boost.Beast server broadcasting JSON to clients.
    - **Graceful Shutdown**: `SetConsoleCtrlHandler` ensures all OHLC data is flushed to disk on `Ctrl+C`.

### 3. Frontend Dashboard
- **Files**: [frontend/index.html](file:///c:/ProjectCPP/Anti/Market/frontend/index.html), [frontend/app.js](file:///c:/ProjectCPP/Anti/Market/frontend/app.js)
- **Function**: Real-time visualization of market data.
- **Key Features**:
    - Connects to `ws://localhost:9002`.
    - Dynamic price updates with visual indicators (Up/Down).

## How to Build and Run

### Build with CMake
```bash
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=[path_to_vcpkg]/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
```

### Run
1. Start `MarketDataSimulator.exe` (from `Release/` folder).
2. Start `MarketDataClient.exe` (from `Release/` folder).
3. Open [frontend/index.html](file:///c:/ProjectCPP/Anti/Market/frontend/index.html) in any modern web browser.

## Verification
- **FIX Handshake**: Verify "Logon" in both simulator and client terminals.
- **Live Prices**: Watch the dashboard for updating Bid/Ask prices.
- **OHLC Data**: Check the `ohlc_data/` folder for generated CSV files.
- **Shutdown**: Press `Ctrl+C` in the client terminal and verify console output showing flushing of OHLC bars.
