//
// Created by Axel Naumann on 09/07/15.
//

#ifndef ROOT7_THISTDRAWABLE_H
#define ROOT7_THISTDRAWABLE_H

#include "ROOT/TCoopPtr.h"
#include "ROOT/TDrawable.h"
#include "ROOT/THistDrawOptions.h"
#include "ROOT/TLogger.h"

#include <memory>

namespace ROOT {

template<int DIMENSIONS, class PRECISION> class THist;

namespace Internal {

template <int DIMENSION>
class THistPainterBase {
  static THistPainterBase<DIMENSION>* fgPainter;

protected:
  THistPainterBase() { fgPainter = this; }
  ~THistPainterBase() { fgPainter = nullptr; }

public:
  static THistPainterBase<DIMENSION>* GetPainter() {
    if (!fgPainter)
      R__ERROR_HERE("HIST")
      << "Missing histogram painter, please load libHistPainter";
    return fgPainter;
  }

  /// Paint a THist. All we need is access to its GetBinContent()
  virtual void Paint(TDrawable& obj, THistDrawOptions<DIMENSION> opts) = 0;
};

template <int DIMENSION>
THistPainterBase<DIMENSION>* THistPainterBase<DIMENSION>::fgPainter = 0;


template <int DIMENSION, class PRECISION>
class THistDrawable final: public TDrawable {
private:
  TCoopPtr<THist<DIMENSION, PRECISION>> fHist;
  THistDrawOptions<DIMENSION> fOpts;

public:
  THistDrawable(TCoopPtr<THist<DIMENSION, PRECISION>> hist,
                THistDrawOptions<DIMENSION> opts): fHist(hist), fOpts(opts) {}

  ~THistDrawable() = default;

  /// Paint the histogram
  void Paint() final {
    THistPainterBase<DIMENSION>::GetPainter()->Paint(*this, fOpts);
  }
};

} // namespace Internal
} // namespace ROOT

#endif //ROOT7_THISTDRAWABLE_H
