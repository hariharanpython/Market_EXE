#pragma once

#include <bits/stdc++.h>
#include <quickfix/Application.h>
#include <quickfix/MessageCracker.h>
#include <quickfix/Session.h>
#include <quickfix/Values.h>
#include <quickfix/fix44/MarketDataIncrementalRefresh.h>
#include <quickfix/fix44/MarketDataRequest.h>
#include <quickfix/fix44/MarketDataSnapshotFullRefresh.h>

#include "../Logger.h"
#include "OHLCBarAggregator.h"

using namespace std;
using namespace FIX;
using namespace Logger;

struct WSMessage {
  string symbol;
  double bid;
  double ask;
  long long timestamp;
};

class FIXMarketDataApp : public Application, public MessageCracker {
public:
  FIXMarketDataApp(OHLCBarAggregator &ohlc) : m_ohlc(ohlc) {
    m_lastStatusUpdate = chrono::system_clock::now();
  }

  void onCreate(const SessionID &sessionID) noexcept override {
    info("Session created: " + sessionID.toString());
  }
  void onLogon(const SessionID &sessionID) noexcept override {
    info("Logon: " + sessionID.toString());
    subscribe(sessionID, "EURUSD");
    subscribe(sessionID, "GBPUSD");
    subscribe(sessionID, "USDJPY");
  }
  void onLogout(const SessionID &sessionID) noexcept override {
    info("Logout: " + sessionID.toString());
  }

  void toAdmin(Message &message, const SessionID &sessionID) noexcept override {
  }
  void toApp(Message &message, const SessionID &sessionID) noexcept override {}
  void fromAdmin(const Message &message,
                 const SessionID &sessionID) noexcept override {}
  void fromApp(const Message &message,
               const SessionID &sessionID) noexcept override {
    crack(message, sessionID);
    checkStatusUpdate();
  }

  void onMessage(const FIX44::MarketDataSnapshotFullRefresh &message,
                 const SessionID &) override {
    Symbol symbol;
    message.get(symbol);

    NoMDEntries noMDEntries;
    message.get(noMDEntries);

    FIX44::MarketDataSnapshotFullRefresh::NoMDEntries group;
    double bid = 0, ask = 0, last = 0;

    for (int i = 1; i <= noMDEntries; ++i) {
      message.getGroup(i, group);
      MDEntryType type;
      MDEntryPx px;
      group.get(type);
      group.get(px);

      if (type == MDEntryType_BID)
        bid = px;
      else if (type == MDEntryType_OFFER)
        ask = px;
      else if (type == MDEntryType_TRADE) {
        last = px;
        m_ohlc.onPrice(symbol.getString(), px, 0);
      }
    }

    if (bid > 0 && ask > 0) {
      pushWSUpdate(symbol.getString(), bid, ask);
    }
  }

  void onMessage(const FIX44::MarketDataIncrementalRefresh &message,
                 const SessionID &) override {
    NoMDEntries noMDEntries;
    message.get(noMDEntries);

    FIX44::MarketDataIncrementalRefresh::NoMDEntries group;
    for (int i = 1; i <= noMDEntries; ++i) {
      message.getGroup(i, group);
      MDEntryType type;
      Symbol symbol;
      MDEntryPx px;
      MDEntrySize size;

      group.get(type);
      group.get(symbol);
      group.get(px);
      group.get(size);

      string sym = symbol.getString();
      if (type == MDEntryType_TRADE) {
        m_ohlc.onPrice(sym, px, (long)size);
        info("Trade: " + sym + " Price=" + to_string(px.getValue()) +
             " Volume=" + to_string((long)size));

        pushWSUpdate(sym, px.getValue() - 0.0001, px.getValue() + 0.0001);
      } else if (type == MDEntryType_BID) {
        pushWSUpdate(sym, px.getValue(), -1.0);
      } else if (type == MDEntryType_OFFER) {
        pushWSUpdate(sym, -1.0, px.getValue());
      }
    }
  }

  bool popWSUpdate(WSMessage &msg) {
    lock_guard<mutex> lock(m_wsMutex);
    if (m_wsQueue.empty())
      return false;
    msg = m_wsQueue.front();
    m_wsQueue.pop();
    return true;
  }

private:
  void subscribe(const SessionID &sessionID, const string &symbol) {
    FIX44::MarketDataRequest request;
    request.set(MDReqID("MD_" + symbol));
    request.set(
        SubscriptionRequestType(SubscriptionRequestType_SNAPSHOT_PLUS_UPDATES));
    request.set(MarketDepth(1));
    request.set(MDUpdateType(MDUpdateType_INCREMENTAL_REFRESH));

    FIX44::MarketDataRequest::NoMDEntryTypes entryType;
    entryType.set(MDEntryType(MDEntryType_BID));
    request.addGroup(entryType);
    entryType.set(MDEntryType(MDEntryType_OFFER));
    request.addGroup(entryType);
    entryType.set(MDEntryType(MDEntryType_TRADE));
    request.addGroup(entryType);

    FIX44::MarketDataRequest::NoRelatedSym symbolGroup;
    symbolGroup.set(Symbol(symbol));
    request.addGroup(symbolGroup);

    Session::sendToTarget(request, sessionID);
    info("Subscribing to market data for: " + symbol);
  }

  void pushWSUpdate(const string &symbol, double bid, double ask) {
    lock_guard<mutex> lock(m_wsMutex);
    WSMessage msg;
    msg.symbol = symbol;
    msg.bid = bid;
    msg.ask = ask;
    msg.timestamp = chrono::duration_cast<chrono::seconds>(
                        chrono::system_clock::now().time_since_epoch())
                        .count();
    m_wsQueue.push(msg);
    if (m_wsQueue.size() > 100)
      m_wsQueue.pop(); // Prevent overflow
  }

  void checkStatusUpdate() {
    auto now = chrono::system_clock::now();
    if (chrono::duration_cast<chrono::seconds>(now - m_lastStatusUpdate)
            .count() >= 60) {
      m_ohlc.printCurrentState();
      m_lastStatusUpdate = now;
    }
  }

  OHLCBarAggregator &m_ohlc;
  queue<WSMessage> m_wsQueue;
  mutex m_wsMutex;
  chrono::system_clock::time_point m_lastStatusUpdate;
};
