//
// Created by Axel Naumann on 06/07/15.
//

#ifndef ROOT_TFIT_H
#define ROOT_TFIT_H

#include <array>
#include <functional>

namespace ROOT {
class TFitResult {

};

template <int DIMENSION, class PRECISION> class THist;

template <int DIMENSION>
class TFunction {
public:
  TFunction(std::function<double (const std::array<double, DIMENSION>&,
                                  const std::array_view<double>& par)> func) {}
};

template <int DIMENSION, class PRECISION>
TFitResult Fit(const THist<DIMENSION, PRECISION>& hist,
               const TFunction<DIMENSION>& func,
               std::array_view<double> paramInit){
  return TFitResult();
}

}

#endif //ROOT_TFIT_H
