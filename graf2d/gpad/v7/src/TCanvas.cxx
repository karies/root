//
// Created by Axel Naumann on 10/07/15.
//

#include "ROOT/TCanvas.h"

#include "ROOT/TDrawable.h"

void ROOT::TCanvas::Paint() {
  for (auto&& drw: fPrimitives) {
    drw->Paint();
  }
}

namespace {
static
std::vector<ROOT::TCoopPtr<ROOT::TCanvas>>& GetHeldCanvases() {
  static std::vector<ROOT::TCoopPtr<ROOT::TCanvas>> sCanvases;
  return sCanvases;
}
};

const std::vector<ROOT::TCoopPtr<ROOT::TCanvas>> &
ROOT::TCanvas::GetCanvases() {
  return GetHeldCanvases();
}

ROOT::TCoopPtr<ROOT::TCanvas> ROOT::TCanvas::Create(
   std::experimental::string_view name) {
  // TODO: name registration (TDirectory?)
  auto pCanvas = TCoopPtr<TCanvas>(new TCanvas());
  GetHeldCanvases().emplace_back(pCanvas);
  return pCanvas;
}