//
// Created by Axel Naumann on 04/09/15.
//

#ifndef ROOT7_THISTDRAWOPTIONS_H
#define ROOT7_THISTDRAWOPTIONS_H

namespace ROOT {

namespace Internal {
template <int DIMENSION>
struct THistDrawOptionsEnum;

/// Specialization containing 1D hist drawing options.
template <>
struct THistDrawOptionsEnum<1> {
  enum EOpts {
    kErrors,
    kBar,
    kText
  };
};


/// Specialization containing 2D hist drawing options.
template <>
struct THistDrawOptionsEnum<2> {
  enum EOpts {
    kBox,
    kText,
    kLego
  };
};

/// Specialization containing 3D hist drawing options.
template <>
struct THistDrawOptionsEnum<3> {
  enum EOpts {
    kLego,
    kIso
  };
};

}

/** \class THistDrawOptions
 Drawing options for a histogram with DIMENSIONS
 */
template<int DIMENSION>
class THistDrawOptions {
  int fOpts;
public:
  THistDrawOptions() = default;
  constexpr THistDrawOptions(typename Internal::THistDrawOptionsEnum<DIMENSION>::EOpts opt): fOpts(2 >> opt) {}
};

namespace Hist {
static constexpr const THistDrawOptions<2> box(Internal::THistDrawOptionsEnum<2>::kBox);
static constexpr const THistDrawOptions<2> text(Internal::THistDrawOptionsEnum<2>::kText);
}
}


#endif //ROOT7_THISTDRAWOPTIONS_H
