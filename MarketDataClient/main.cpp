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
  // init("log", "client.log"); // Moved below argument parsing

  ix::initNetSystem();

  try {
    string configPath = "client.cfg";
    int wsPort = 9002;
    string clientId = "1";

    if (argc > 1)
      configPath = argv[1];

    if (!fs::exists(configPath)) {
      throw runtime_error("Configuration file '" + configPath + "' not found.");
    }

    SessionSettings settings(configPath);
    int updateIntervalMs = 1000;

    try {
      const Dictionary &defaults = settings.get();
      if (defaults.has("WebSocketPort"))
        wsPort = stoi(defaults.getString("WebSocketPort"));
      if (defaults.has("ClientID"))
        clientId = defaults.getString("ClientID");
      if (defaults.has("FrontendUpdateInterval"))
        updateIntervalMs = stoi(defaults.getString("FrontendUpdateInterval"));
    } catch (...) {
    }

    // CLI overrides config file
    if (argc > 2)
      wsPort = stoi(argv[2]);
    if (argc > 3)
      clientId = argv[3];

    info("Starting FIX Market Data Client [" + clientId +
         "] with config: " + configPath + " and WS port: " + to_string(wsPort));
    init("log_" + clientId, "client_" + clientId + ".log");

    if (!fs::exists("logs"))
      fs::create_directory("logs");
    if (!fs::exists("store"))
      fs::create_directory("store");
    string ohlcDir = "OHLC_price_data_" + clientId;
    if (!fs::exists(ohlcDir))
      fs::create_directory(ohlcDir);

    map<string, WSMessage> priceCache;
    mutex cacheMutex;

    ix::WebSocketServer wsServer(wsPort, "0.0.0.0");
    wsServer.setOnClientMessageCallback(
        [&priceCache, &cacheMutex](
            shared_ptr<ix::ConnectionState> connectionState,
            ix::WebSocket &webSocket, const ix::WebSocketMessagePtr &msg) {
          if (msg->type == ix::WebSocketMessageType::Open) {
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
      error("WebSocket server failed to start on port " + to_string(wsPort) +
            ": " + res.second);
    } else {
      wsServer.start();
      info("WebSocket server started on port " + to_string(wsPort));
    }

    OHLCBarAggregator ohlc(clientId);
    g_ohlc = &ohlc;

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
            j["timestamp"] =
                chrono::duration_cast<chrono::seconds>(now.time_since_epoch())
                    .count();
            string payload = j.dump();
            for (auto &&client : wsServer.getClients()) {
              client->send(payload);
            }
          }
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
