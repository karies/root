//
// Created by Axel Naumann on 08/07/15.
//

#ifndef ROOT7_TCANVAS_H
#define ROOT7_TCANVAS_H

#include <experimental/string_view>
#include <vector>

#include "ROOT/TCoopPtr.h"
#include "ROOT/TDrawable.h"

namespace ROOT {

/** \class TCanvas
  Graphic container for `TDrawable`-s.
  */

class TCanvas {
  std::vector<std::unique_ptr<Internal::TDrawable>> fPrimitives;

  /// We need to keep track of canvases; please use Create()
  TCanvas() = default;

public:
  static TCoopPtr<TCanvas> Create();
  static TCoopPtr<TCanvas> Create(std::experimental::string_view name);

  /// Add a something to be painted. The pad claims shared ownership.
  template <class T>
  void Draw(TCoopPtr<T> what) {
    // Requires GetDrawable(what, options) to be known!
    fPrimitives.emplace_back(GetDrawable(what));
  }

  /// Add a something to be painted, with options. The pad claims shared ownership.
  template <class T, class OPTIONS>
  void Draw(TCoopPtr<T> what, const OPTIONS& options) {
    // Requires GetDrawable(what, options) to be known!
    fPrimitives.emplace_back(GetDrawable(what, options));
  }

  void Paint();

  static const std::vector<TCoopPtr<TCanvas>>& GetCanvases();
};

}

#endif // ROOT7_TCANVAS_H
