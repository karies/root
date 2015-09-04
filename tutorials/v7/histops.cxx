//
// Created by Axel Naumann on 08/08/15.
//


#include "ROOT/THist.h"
#include <iostream>

int main(int, const char*[]) {

  // Create a 2D histogram with an X axis with equidistant bins, and a y axis
  // with irregular binning.
  ROOT::TH2D hist1({100, 0., 1.}, {{0., 1., 2., 3.,10.}});

  // Fill weight 1. at the coordinate 0.01, 1.02.
  hist1.Fill({0.01, 1.02});


  ROOT::TH2D hist2({{ {10, 0., 1.}, {{0., 1., 2., 3.,10.}} }});
  // Fill weight 1. at the coordinate 0.01, 1.02.
  hist2.Fill({0.01, 1.02});

  ROOT::Add(hist1, hist2);

  int binidx = hist1.GetImpl()->GetBinIndex({0.01, 1.02});
  std::cout << hist1.GetImpl()->GetBinContent(binidx) << std::endl;

  return 0;
}
