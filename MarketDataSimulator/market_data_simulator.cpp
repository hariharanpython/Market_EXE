#include <bits/stdc++.h>
#include <quickfix/Application.h>
#include <quickfix/FileLog.h>
#include <quickfix/FileStore.h>
#include <quickfix/MessageCracker.h>
#include <quickfix/Mutex.h>
#include <quickfix/SessionSettings.h>
#include <quickfix/SocketAcceptor.h>
#include <quickfix/Values.h>
#include <quickfix/fix44/MarketDataIncrementalRefresh.h>
#include <quickfix/fix44/MarketDataRequest.h>
#include <quickfix/fix44/MarketDataSnapshotFullRefresh.h>

#include "../Logger.h"
#include <windows.h>

using namespace std;
using namespace FIX;
using namespace Logger;
namespace fs = filesystem;

class MarketDataSimulator : public Application, public MessageCracker {
public:
  MarketDataSimulator() : m_running(true) {
    m_prices["EURUSD"] = 1.08500;
    m_prices["GBPUSD"] = 1.27000;
    m_prices["USDJPY"] = 150.000;

    m_updateThread = thread([this]() { priceUpdateLoop(); });
  }

  ~MarketDataSimulator() {
    m_running = false;
    if (m_updateThread.joinable())
      m_updateThread.join();
  }

  // Application overrides
  void onCreate(const SessionID &sessionID) noexcept override {
    info("Session created: " + sessionID.toString());
  }
  void onLogon(const SessionID &sessionID) noexcept override {
    info("Logon: " + sessionID.toString());
  }
  void onLogout(const SessionID &sessionID) noexcept override {
    info("Logout: " + sessionID.toString());
    lock_guard<mutex> lock(m_mutex);
    for (auto &pair : m_subscriptions) {
      pair.second.erase(sessionID);
    }
  }

  void toAdmin(Message &message, const SessionID &sessionID) noexcept override {
  }
  void toApp(Message &message, const SessionID &sessionID) noexcept override {
    crack(message, sessionID);
  }
  void fromAdmin(const Message &message,
                 const SessionID &sessionID) noexcept override {}
  void fromApp(const Message &message,
               const SessionID &sessionID) noexcept override {
    crack(message, sessionID);
  }

  // MessageCracker overrides
  void onMessage(const FIX44::MarketDataRequest &message,
                 const SessionID &sessionID) override {
    info("Received Market Data Request");

    MDReqID mdReqID;
    SubscriptionRequestType subType;
    NoRelatedSym noRelatedSym;

    message.get(mdReqID);
    message.get(subType);
    message.get(noRelatedSym);

    FIX44::MarketDataRequest::NoRelatedSym group;
    for (int i = 1; i <= noRelatedSym; ++i) {
      message.getGroup(i, group);
      Symbol symbol;
      group.get(symbol);

      lock_guard<mutex> lock(m_mutex);
      if (m_prices.count(symbol.getString())) {
        if (subType == SubscriptionRequestType_SNAPSHOT_PLUS_UPDATES) {
          m_subscriptions[symbol.getString()].insert(sessionID);
          info("Subscribed: " + symbol.getString());
        }
        sendSnapshot(symbol.getString(), sessionID, mdReqID.getString());
        info("Sent snapshot for " + symbol.getString() + " @ " +
             to_string(m_prices[symbol.getString()]));
      }
    }
  }

private:
  void sendSnapshot(const string &symbol, const SessionID &sessionID,
                    const string &mdReqID) {
    FIX44::MarketDataSnapshotFullRefresh snapshot;
    snapshot.set(MDReqID(mdReqID));
    snapshot.set(Symbol(symbol));

    double price = m_prices[symbol];

    // Bid
    FIX44::MarketDataSnapshotFullRefresh::NoMDEntries bidGroup;
    bidGroup.set(MDEntryType(MDEntryType_BID));
    bidGroup.set(MDEntryPx(price - 0.0002));
    snapshot.addGroup(bidGroup);

    // Offer
    FIX44::MarketDataSnapshotFullRefresh::NoMDEntries offerGroup;
    offerGroup.set(MDEntryType(MDEntryType_OFFER));
    offerGroup.set(MDEntryPx(price + 0.0002));
    snapshot.addGroup(offerGroup);

    // Trade
    FIX44::MarketDataSnapshotFullRefresh::NoMDEntries tradeGroup;
    tradeGroup.set(MDEntryType(MDEntryType_TRADE));
    tradeGroup.set(MDEntryPx(price));
    snapshot.addGroup(tradeGroup);

    Session::sendToTarget(snapshot, sessionID);
  }

  void priceUpdateLoop() {
    default_random_engine generator;
    uniform_real_distribution<double> walk(-0.0001, 0.0001);
    uniform_int_distribution<long> volDist(10000, 100000);

    while (m_running) {
      this_thread::sleep_for(chrono::milliseconds(100));

      lock_guard<mutex> lock(m_mutex);
      for (auto &pair : m_prices) {
        const string &symbol = pair.first;
        double &price = pair.second;

        // Adjust walk for JPY
        double step = walk(generator);
        if (symbol == "USDJPY")
          step *= 100.0;
        price += step;

        auto it = m_subscriptions.find(symbol);
        if (it != m_subscriptions.end() && !it->second.empty()) {
          long volume = volDist(generator);
          broadcastUpdate(symbol, price, volume, it->second);
          info("Update: " + symbol + " = " + to_string(price));
        }
      }
    }
  }

  void broadcastUpdate(const string &symbol, double price, long volume,
                       const set<SessionID> &sessions) {
    FIX44::MarketDataIncrementalRefresh refresh;
    FIX44::MarketDataIncrementalRefresh::NoMDEntries group;
    group.set(MDUpdateAction(MDUpdateAction_NEW));
    group.set(MDEntryType(MDEntryType_TRADE));
    group.set(Symbol(symbol));
    group.set(MDEntryPx(price));
    group.set(MDEntrySize(volume));
    refresh.addGroup(group);

    for (const auto &sessionID : sessions) {
      Session::sendToTarget(refresh, sessionID);
    }
  }

  unordered_map<string, double> m_prices;
  unordered_map<string, set<SessionID>> m_subscriptions;
  mutex m_mutex;
  atomic<bool> m_running;
  thread m_updateThread;
};

int main(int argc, char **argv) {
  // Get the absolute path of the executable using Windows API
  wchar_t exePathW[MAX_PATH];
  GetModuleFileNameW(NULL, exePathW, MAX_PATH);
  fs::path exePath = fs::path(exePathW);
  fs::path exeDir = exePath.parent_path();

  // Set working directory to exe location
  fs::current_path(exeDir);

  init("log", "simulator.log");

  try {
    info("Executable path : " + exePath.string());
    info("Working directory: " + exeDir.string());

    if (!fs::exists("log"))
      fs::create_directory("log");
    if (!fs::exists("store"))
      fs::create_directory("store");

    fs::path cfgPath = exeDir / "server.cfg";

    // Log the exact path being checked
    info("Looking for config at: " + cfgPath.string());

    if (!fs::exists(cfgPath)) {
      // Also list all files in the directory to help diagnose
      error("server.cfg NOT found at: " + cfgPath.string());
      info("Files found in exe directory:");
      for (const auto &entry : fs::directory_iterator(exeDir)) {
        info("  -> " + entry.path().string());
      }
      throw runtime_error("Configuration file 'server.cfg' not found in: " +
                          exeDir.string());
    }

    info("server.cfg FOUND at: " + cfgPath.string());

    SessionSettings settings(cfgPath.string());
    MarketDataSimulator application;
    FileStoreFactory storeFactory(settings);
    FileLogFactory logFactory(settings);
    SocketAcceptor acceptor(application, storeFactory, settings, logFactory);

    acceptor.start();
    info("Simulator is running. Press CTRL+C to quit.");
    while (true) {
      this_thread::sleep_for(chrono::seconds(1));
    }
    acceptor.stop();

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
