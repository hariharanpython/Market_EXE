#pragma once

#include <bits/stdc++.h>

using namespace std;
namespace fs = filesystem;

namespace Logger {
enum Level { Info, Warning, Error };

inline string m_logPath;
inline ofstream m_logFile;
inline mutex m_mutex;

inline void init(const string &logDir = "log",
                 const string &fileName = "application.log") {
  lock_guard<mutex> lock(m_mutex);
  if (!fs::exists(logDir)) {
    fs::create_directories(logDir);
  }
  m_logPath = logDir + "/" + fileName;

  if (m_logFile.is_open()) {
    m_logFile.close();
  }
  m_logFile.open(m_logPath, ios::app);
}

inline void log(Level level, const string &message) {
  lock_guard<mutex> lock(m_mutex);

  string levelStr;
  switch (level) {
  case Info:
    levelStr = "INFO";
    break;
  case Warning:
    levelStr = "WARN";
    break;
  case Error:
    levelStr = "ERR ";
    break;
  }

  auto now = chrono::system_clock::now();
  auto in_time_t = chrono::system_clock::to_time_t(now);
  auto ms = chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch())
                .count() %
            1000;

  stringstream ss;
  ss << "[" << put_time(localtime(&in_time_t), "%Y-%m-%d %H:%M:%S") << "."
     << setfill('0') << setw(3) << ms << "] "
     << "[" << levelStr << "] " << message;

  string logEntry = ss.str();

  // Console output
  if (level == Error) {
    cerr << logEntry << endl;
  } else {
    cout << logEntry << endl;
  }

  // Persistent file output
  if (m_logFile.is_open()) {
    m_logFile << logEntry << endl;
    m_logFile.flush(); // Ensure it's written immediately
  }
}

inline void info(const string &message) { log(Info, message); }
inline void warn(const string &message) { log(Warning, message); }
inline void error(const string &message) { log(Error, message); }
} // namespace Logger
