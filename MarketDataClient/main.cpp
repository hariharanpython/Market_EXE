#define ssize_t quickfix_ssize_t
#include <bits/stdc++.h>
#include <quickfix/FileLog.h>
#include <quickfix/FileStore.h>
#include <quickfix/SessionSettings.h>
#include <quickfix/SocketInitiator.h>
#undef ssize_t

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocketServer.h>
#include <nlohmann/json.hpp>

#include "../Logger.h"
#include "FIXMarketDataApp.h"
#include <windows.h>

using json = nlohmann::json;
using namespace std;
using namespace Logger;
using namespace FIX;
namespace fs = filesystem;

// Global for signal handling
atomic<bool> g_running(true);
OHLCBarAggregator *g_ohlc = nullptr;
SocketInitiator *g_initiator = nullptr;

BOOL WINAPI ConsoleHandler(DWORD dwType) {
  if (dwType == CTRL_C_EVENT || dwType == CTRL_BREAK_EVENT) {
    info("Shutdown signal received...");
    g_running = false;
    if (g_initiator)
      g_initiator->stop();
    if (g_ohlc)
      g_ohlc->flushAll();
    return TRUE;
  }
  return FALSE;
}

int main(int argc, char **argv) {
  SetConsoleCtrlHandler(ConsoleHandler, TRUE);
  init("log", "client.log");

  ix::initNetSystem();

  try {
    info("Starting FIX Market Data Client...");

    // Ensure directories exist
    if (!fs::exists("logs"))
      fs::create_directory("logs");
    if (!fs::exists("store"))
      fs::create_directory("store");
    if (!fs::exists("OHLC_price_data"))
      fs::create_directory("OHLC_price_data");

    if (!fs::exists("client.cfg")) {
      throw runtime_error("Configuration file 'client.cfg' not found in " +
                          fs::current_path().string());
    }

    map<string, WSMessage> priceCache;
    mutex cacheMutex;

    // Initialize WebSocket Server
    int wsPort = 9002;
    ix::WebSocketServer wsServer(wsPort, "0.0.0.0");
    wsServer.setOnClientMessageCallback(
        [&priceCache, &cacheMutex](
            shared_ptr<ix::ConnectionState> connectionState,
            ix::WebSocket &webSocket, const ix::WebSocketMessagePtr &msg) {
          if (msg->type == ix::WebSocketMessageType::Open) {
            info("New frontend connection established");

            // Send initial snapshot of current prices immediately
            lock_guard<mutex> lock(cacheMutex);
            for (auto const &[symbol, latestMsg] : priceCache) {
              json j;
              j["symbol"] = latestMsg.symbol;
              j["bid"] = latestMsg.bid;
              j["ask"] = latestMsg.ask;
              j["timestamp"] =
                  chrono::duration_cast<chrono::seconds>(
                      chrono::system_clock::now().time_since_epoch())
                      .count();
              webSocket.send(j.dump());
            }
          }
        });

    auto res = wsServer.listen();
    if (!res.first) {
      error("WebSocket server failed to start: " + res.second);
    } else {
      wsServer.start();
      info("WebSocket server started on port " + to_string(wsPort));
    }

    OHLCBarAggregator ohlc;
    g_ohlc = &ohlc;

    SessionSettings settings("client.cfg");

    // Read frontend update interval from settings
    int updateIntervalMs = 1000; // Default
    try {
      const Dictionary &defaults = settings.get();
      if (defaults.has("FrontendUpdateInterval")) {
        updateIntervalMs = stoi(defaults.getString("FrontendUpdateInterval"));
      }
    } catch (...) {
      info("Using default frontend update interval (1000ms)");
    }
    info("Frontend update interval set to " + to_string(updateIntervalMs) +
         "ms");

    FIXMarketDataApp app(ohlc);
    FileStoreFactory storeFactory(settings);
    FileLogFactory logFactory(settings);
    SocketInitiator initiator(app, storeFactory, settings, logFactory);
    g_initiator = &initiator;

    initiator.start();

    info("Client is running. Press CTRL+C to quit.");

    auto lastFrontendUpdate = chrono::system_clock::now();

    while (g_running) {
      auto now = chrono::system_clock::now();
      auto elapsed =
          chrono::duration_cast<chrono::milliseconds>(now - lastFrontendUpdate)
              .count();

      // Always drain the queue to keep the cache fresh
      WSMessage msg;
      {
        lock_guard<mutex> lock(cacheMutex);
        while (app.popWSUpdate(msg)) {
          priceCache[msg.symbol] = msg;
        }
      }

      if (elapsed >= updateIntervalMs) {
        lock_guard<mutex> lock(cacheMutex);
        if (!priceCache.empty()) {
          for (auto &[symbol, latestMsg] : priceCache) {
            json j;
            j["symbol"] = symbol;
            j["bid"] = latestMsg.bid;
            j["ask"] = latestMsg.ask;
            // Use current time to show the frontend that we are still alive
            j["timestamp"] =
                chrono::duration_cast<chrono::seconds>(now.time_since_epoch())
                    .count();

            string payload = j.dump();
            for (auto &&client : wsServer.getClients()) {
              client->send(payload);
            }
          }
          info("Broadcasted heartbeat update for " +
               to_string(priceCache.size()) + " symbols");
        }
        lastFrontendUpdate = now;
      }

      this_thread::sleep_for(chrono::milliseconds(50));
    }

    initiator.stop();
    wsServer.stop();
    info("Client shut down cleanly.");
  } catch (ConfigError &e) {
    error("FIX Configuration Error: " + string(e.what()));
    return 1;
  } catch (RuntimeError &e) {
    error("FIX Runtime Error: " + string(e.what()));
    return 1;
  } catch (exception &e) {
    error("Error: " + string(e.what()));
    return 1;
  }
  return 0;
}
