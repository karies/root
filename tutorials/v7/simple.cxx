//
//  simple.cxx
//  ROOT7
//
//  Created by Axel Naumann on 22/03/15.
//
//

#include "ROOT/THist.h"
#include "ROOT/TFit.h"
#include "ROOT/TFile.h"

int main(int, const char*[]) {

  // Create a 2D histogram with an X axis with equidistant bins, and a y axis
  // with irregular binning.
  ROOT::TAxisConfig xAxis(100, 0., 1.);
  ROOT::TAxisConfig yAxis({0., 1., 2., 3.,10.});
  ROOT::TH2D histFromVars(xAxis, yAxis);

  // Or the short in-place version:
  // Create a 2D histogram with an X axis with equidistant bins, and a y axis
  // with irregular binning.
  ROOT::TH2D hist({100, 0., 1.}, {{0., 1., 2., 3.,10.}});

  // Fill weight 1. at the coordinate 0.01, 1.02.
  hist.Fill({0.01, 1.02});

  // Fit the histogram.
  ROOT::TFunction<2> func([](const std::array<double,2>& x,
                             const std::array_view<double>& par)
                          { return par[0]*x[0]*x[0] + (par[1]-x[1])*x[1]; });

  ROOT::TFitResult fitResult = ROOT::Fit(hist, func, {{0., 1.}});

  ROOT::TCoopPtr<ROOT::TFile> file = ROOT::TFile::Recreate("hist.root");
  file->Write("TheHist", &hist);

  return 0;
}
