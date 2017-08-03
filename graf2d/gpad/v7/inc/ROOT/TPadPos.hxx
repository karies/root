/// \file ROOT/TPadPos.hxx
/// \ingroup Gpad ROOT7
/// \author Axel Naumann <axel@cern.ch>
/// \date 2017-07-07
/// \warning This is part of the ROOT 7 prototype! It will change without notice. It might trigger earthquakes. Feedback
/// is welcome!

/*************************************************************************
 * Copyright (C) 1995-2017, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT7_TPadPos
#define ROOT7_TPadPos

#include "ROOT/TPadCoord.hxx"

namespace ROOT {
namespace Experimental {

namespace Internal {
/** \class ROOT::Experimental::Internal::TPadHorizVert
  A 2D (horizontal and vertical) combination of `TPadCoord`s.
  */

template <class DERIVED>
struct TPadHorizVert {
   TPadCoord fHoriz; ///< Horizontal position
   TPadCoord fVert; ///< Vertical position

   /// Add two `TPadPos`s.
   friend DERIVED operator+(DERIVED lhs, const DERIVED &rhs)
   {
      return {lhs.fHoriz + rhs.fHoriz, lhs.fVert + rhs.fVert};
   }

   /// Subtract two `TPadPos`s.
   friend DERIVED operator-(DERIVED lhs, const DERIVED &rhs)
   {
      return {lhs.fHoriz - rhs.fHoriz, lhs.fVert - rhs.fVert};
   }

   DERIVED &toDerived() { return *static_cast<DERIVED*>(this); }

   /// Add a `TPadPos`.
   DERIVED &operator+=(const DERIVED &rhs)
   {
      fHoriz += rhs.fHoriz;
      fVert += rhs.fVert;
      return toDerived();
   };

   /// Subtract a `TPadPos`.
   DERIVED &operator-=(const DERIVED &rhs)
   {
      fHoriz -= rhs.fHoriz;
      fVert -= rhs.fVert;
      return toDerived();
   };

   /** \class ScaleFactor
      A scale factor (separate factors for horizontal and vertical) for scaling a `TPadCoord`.
      */
   struct ScaleFactor {
      double fHoriz; ///< Horizontal scale factor
      double fVert; ///< Vertical scale factor
   };

   /// Scale a `TPadHorizVert` horizonally and vertically.
   /// \param scale - the scale factor, 
   DERIVED &operator*=(const ScaleFactor& scale)
   {
      fHoriz *= scale.fHoriz;
      fVert *= scale.fVert;
      return toDerived();
   };
};
};

/** \class ROOT::Experimental::TPadPos
  A position (horizontal and vertical) in a `TPad`.
  */
struct TPadPos: Internal::TPadHorizVert<TPadPos> {
};

} // namespace Experimental
} // namespace ROOT

#endif
