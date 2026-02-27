#pragma once

#include "../Logger.h"
#include <bits/stdc++.h>

using namespace std;
using namespace Logger;
namespace fs = filesystem;

struct OHLCBar {
  chrono::system_clock::time_point timestamp;
  double open = 0.0, high = 0.0, low = 0.0, close = 0.0;
  long volume = 0;
  int tick_count = 0;

  void update(double price, long vol = 0) {
    if (tick_count == 0) {
      open = high = low = close = price;
    } else {
      if (price > high)
        high = price;
      if (price < low)
        low = price;
      close = price;
    }
    volume += vol;
    tick_count++;
  }

  bool isEmpty() const { return tick_count == 0; }
};

enum class Timeframe {
  SEC_1 = 1,
  SEC_5 = 5,
  SEC_10 = 10,
  SEC_15 = 15,
  SEC_30 = 30,
  MIN_1 = 60,
  MIN_5 = 300,
  MIN_15 = 900,
  MIN_30 = 1800,
  HOUR_1 = 3600,
  HOUR_4 = 14400,
};

class OHLCBarAggregator {
public:
  OHLCBarAggregator(string clientId = "1") : m_clientId(clientId) {
    string dataDir = "./OHLC_price_data_" + m_clientId;
    if (!fs::exists(dataDir)) {
      fs::create_directories(dataDir);
    }
    m_timeframes = {Timeframe::SEC_1,  Timeframe::SEC_5,  Timeframe::SEC_10,
                    Timeframe::SEC_15, Timeframe::SEC_30, Timeframe::MIN_1,
                    Timeframe::MIN_5,  Timeframe::MIN_15, Timeframe::MIN_30,
                    Timeframe::HOUR_1, Timeframe::HOUR_4};
  }

  void onPrice(const string &symbol, double price, long volume) {
    lock_guard<mutex> lock(m_mutex);
    auto now = chrono::system_clock::now();
    auto epoch =
        chrono::duration_cast<chrono::seconds>(now.time_since_epoch()).count();

    for (auto tf : m_timeframes) {
      int seconds = static_cast<int>(tf);
      long long bucket = (epoch / seconds) * seconds;
      auto bucket_tp = chrono::system_clock::from_time_t(bucket);

      auto &bar = m_bars[symbol][tf];

      if (!bar.isEmpty() && bar.timestamp != bucket_tp) {
        saveToCSV(symbol, tf, bar);
        bar = OHLCBar(); // Reset
      }

      if (bar.isEmpty()) {
        bar.timestamp = bucket_tp;
      }
      bar.update(price, volume);
    }
  }

  void flushAll() {
    lock_guard<mutex> lock(m_mutex);
    for (auto &symbol_pair : m_bars) {
      for (auto &tf_pair : symbol_pair.second) {
        if (!tf_pair.second.isEmpty()) {
          saveToCSV(symbol_pair.first, tf_pair.first, tf_pair.second);
        }
      }
    }
  }

  void printCurrentState() {
    lock_guard<mutex> lock(m_mutex);
    info("--- Current OHLC State ---");
    for (auto &symbol_pair : m_bars) {
      info("Symbol: " + symbol_pair.first);
      for (auto &tf_pair : symbol_pair.second) {
        const auto &bar = tf_pair.second;
        if (!bar.isEmpty()) {
          stringstream ss;
          ss << "  TF " << timeframeToString(tf_pair.first) << ": "
             << "O:" << bar.open << " H:" << bar.high << " L:" << bar.low
             << " C:" << bar.close << " V:" << bar.volume
             << " Ticks:" << bar.tick_count;
          info(ss.str());
        }
      }
    }
    info("--------------------------");
  }

private:
  string timeframeToString(Timeframe tf) {
    switch (tf) {
    case Timeframe::SEC_1:
      return "1s";
    case Timeframe::SEC_5:
      return "5s";
    case Timeframe::SEC_10:
      return "10s";
    case Timeframe::SEC_15:
      return "15s";
    case Timeframe::SEC_30:
      return "30s";
    case Timeframe::MIN_1:
      return "1m";
    case Timeframe::MIN_5:
      return "5m";
    case Timeframe::MIN_15:
      return "15m";
    case Timeframe::MIN_30:
      return "30m";
    case Timeframe::HOUR_1:
      return "1h";
    case Timeframe::HOUR_4:
      return "4h";
    default:
      return "unknown";
    }
  }

  void saveToCSV(const string &symbol, Timeframe tf, const OHLCBar &bar) {
    string filename = "./OHLC_price_data_" + m_clientId + "/" + symbol + "_" +
                      timeframeToString(tf) + ".csv";

    bool needsHeader = !fs::exists(filename) || fs::file_size(filename) == 0;

    ofstream file(filename, ios::app);
    if (file.is_open()) {
      if (needsHeader) {
        file << "Timestamp,Open,High,Low,Close,Volume,TickCount\n";
      }
      auto timestamp = chrono::duration_cast<chrono::seconds>(
                           bar.timestamp.time_since_epoch())
                           .count();
      file << timestamp << "," << fixed << setprecision(5) << bar.open << ","
           << bar.high << "," << bar.low << "," << bar.close << ","
           << bar.volume << "," << bar.tick_count << "\n";
    }
  }

  string m_clientId;
  vector<Timeframe> m_timeframes;
  // Using unordered_map for O(1) average lookup performance
  unordered_map<string, unordered_map<Timeframe, OHLCBar>> m_bars;
  mutex m_mutex;
};
