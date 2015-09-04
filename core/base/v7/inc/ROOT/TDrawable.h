//
// Created by Axel Naumann on 08/07/15.
//

#ifndef ROOT7_TDRAWABLE_H
#define ROOT7_TDRAWABLE_H

namespace ROOT {
namespace Internal {

/** \class TDrawable
  Base class for drawable entities: objects that can be painted on a `TPad`.
  */

class TDrawable {
public:
  virtual ~TDrawable();

  /// Paint the object
  virtual void Paint() = 0;
};

} // namespace Internal
} // namespace ROOT

#endif //ROOT7_TDRAWABLE_H
