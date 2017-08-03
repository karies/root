/// \file ROOT/TPadExtent.hxx
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

#ifndef ROOT7_TPadExtent
#define ROOT7_TPadExtent

#include "ROOT/TPadPos.hxx"

namespace ROOT {
namespace Experimental {

/** \class ROOT::Experimental::TPadExtent
  An extent / size (horizontal and vertical) in a `TPad`.
  */
struct TPadExtent: Internal::TPadHorizVert<TPadPos> {
  using Internal::TPadHorizVert<TPadPos>::TPadHorizVert;
};


} // namespace Experimental
} // namespace ROOT

#endif
