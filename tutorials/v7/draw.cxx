//
//  tryouts.cxx
//  ROOT7
//
//  Created by Axel Naumann on 22/03/15.
//
//

#include "ROOT/THist.h"
#include "ROOT/TCanvas.h"
#include "ROOT/TDirectory.h"
#include <iostream>

void example() {
  using namespace ROOT;

  auto pHist = MakeCoop<THist<2, double>>(TAxisConfig{100, 0., 1.},
                                          TAxisConfig{{0., 1., 2., 3.,10.}});

  pHist->Fill({0.01, 1.02});
  TDirectory::Heap().Add("hist", pHist);

  auto canvas = TCanvas::Create("MyCanvas");
  canvas->Draw(pHist);
}

int main(int argc, const char* argv[]) {
  example();

  // And the event loop (?) will call
  for (auto&& canv: ROOT::TCanvas::GetCanvases())
    canv->Paint();

  std::cout << "Press enter..." << std::endl;
  getchar(); // wait for input;
  return 0;
}
