//
// Created by Axel Naumann on 07/07/15.
//

#include "ROOT/TLogger.h"
#include <iostream>

// pin vtable
ROOT::TLogHandler::~TLogHandler() {}

namespace {
class TLogHandlerDefault: public ROOT::TLogHandler {
public:
  // Returns false if further emission of this log entry should be suppressed.
  bool Emit(const ROOT::TLogEntry &entry) override;
};

bool TLogHandlerDefault::Emit(const ROOT::TLogEntry &entry) {
  constexpr static std::array<const char *, 5> sTag{
     "Debug",
     "Info",
     "Warning",
     "Log",
     "FATAL"
  };
  std::cerr << "ROOT ";
  if (!entry.fGroup.empty())
    std::cerr << '[' << entry.fGroup << "] ";
  std::cerr << sTag[static_cast<int>(entry.fLevel)];

  if (!entry.fFile.empty())
    std::cerr << " " << entry.fFile << ':' << entry.fLine;
  if (!entry.fFuncName.empty())
    std::cerr << " in " << entry.fFuncName;
  std::cerr << ":\n" << "   " << entry.str();
  return true;
}
} // unnamed namespace

ROOT::TLogManager& ROOT::TLogManager::Get() {
  static TLogManager instance(std::move(std::make_unique<TLogHandlerDefault>()));
  return instance;
}
