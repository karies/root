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

/** \class ROOT::Experimental::TPadPos
  A position (horizontal and vertical) in a `TPad`.
  */

struct TPadPos {
   TPadCoord fHoriz; ///< Horizontal position
   TPadCoord fVert; ///< Vertical position

   /// Add two `TPadPos`s.
   friend TPadPos operator+(TPadPos lhs, const TPadPos &rhs)
   {
      return {lhs.fHoriz + rhs.fHoriz, lhs.fVert + rhs.fVert};
   }

   /// Subtract two `TPadPos`s.
   friend TPadPos operator-(TPadPos lhs, const TPadPos &rhs)
   {
      return TPadPos{lhs.fHoriz - rhs.fHoriz, lhs.fVert - rhs.fVert};
   }

   /// Add a `TPadPos`.
   TPadPos &operator+=(const TPadPos &rhs)
   {
      fHoriz += rhs.fHoriz;
      fVert += rhs.fVert;
      return *this;
   };

   /// Subtract a `TPadPos`.
   TPadPos &operator-=(const TPadPos &rhs)
   {
      fHoriz -= rhs.fHoriz;
      fVert -= rhs.fVert;
      return *this;
   };

   /** \class ScaleFactor
      A scale factor (separate factors for horizontal and vertical) for scaling a `TPadCoord`.
      */
   struct ScaleFactor {
      double fHoriz; ///< Horizontal scale factor
      double fVert; ///< Vertical scale factor
   };
   /// Scale a `TPadPos` horizonally and vertically.
   /// \param scale.first is the horizontal scale factor, scale.second is the vertical scale factor, 
   TPadPos &operator*=(const ScaleFactor& scale)
   {
      fHoriz *= scale.fHoriz;
      fVert *= scale.fVert;
      return *this;
   };
};

} // namespace Experimental
} // namespace ROOT

#endif
