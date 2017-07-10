/// \file ROOT/TPad.hxx
/// \ingroup Gpad ROOT7
/// \author Axel Naumann <axel@cern.ch>
/// \date 2017-07-06
/// \warning This is part of the ROOT 7 prototype! It will change without notice. It might trigger earthquakes. Feedback
/// is welcome!

/*************************************************************************
 * Copyright (C) 1995-2017, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT7_TPad
#define ROOT7_TPad

#include <memory>
#include <vector>

#include "ROOT/TDrawable.hxx"
#include "ROOT/TPadPos.hxx"
#include "ROOT/TypeTraits.hxx"

namespace ROOT {
namespace Experimental {

namespace Internal {
class TVirtualCanvasPainter;
}

/** \class ROOT::Experimental::TPad
  Graphic container for `TDrawable`-s.
  */

class TPad {
public:
   using Primitives_t = std::vector<std::unique_ptr<TDrawable>>;

private:
   /// Content of the pad.
   Primitives_t fPrimitives;

   /// Pad containing this pad as a sub-pad.
   const TPad *fParent = nullptr;

   /// Size of the pad in the parent's (!) coordinate system.
   TPadPos fSize;

   /// Disable copy construction for now.
   TPad(const TPad &) = delete;

   /// Disable assignment for now.
   TPad &operator=(const TPad &) = delete;

   /** \class TPadDrawable
      Draw a TPad, by drawing its contained graphical elements at the pad offset in the parent pad.'
      */
   class TPadDrawable: public TDrawable {
   private:
      const std::unique_ptr<TPad> fPad; ///< The pad to be painted
      TPadPos fPos;                     ///< Offset with respect to parent TPad.

   public:
      TPadDrawable(std::unique_ptr<TPad> &&pPad, const TPadPos &pos): fPad(std::move(pPad)), fPos(pos) {}

      /// Paint the pad.
      void Paint(Internal::TVirtualCanvasPainter &/*canv*/) final
      {
         // FIXME: and then what? Something with fPad.GetListOfPrimitives()?
      }

      TPad* Get() const { return fPad.get(); }
   };

   friend std::unique_ptr<TPadDrawable> GetDrawable(std::unique_ptr<TPad> &&pad, const TPadPos &pos)
   {
      return std::make_unique<TPadDrawable>(std::move(pad), pos);
   }

   template <class DRAWABLE>
   DRAWABLE &AddDrawable(std::unique_ptr<DRAWABLE> &&uPtr)
   {
      DRAWABLE &drw = *uPtr;
      fPrimitives.emplace_back(std::move(uPtr));
      return drw;
   }

protected:
   /// Create a TPad without parent.
   TPad(const TPadPos& size): fSize(size) {}

public:
   /// Create a child pad.
   TPad(const TPad &parent, const TPadPos& size): fParent(&parent), fSize(size) {}

   /// Divide this pad into a grid of subpad with padding in between.
   /// \param nHoriz Number of horizontal pads.
   /// \param nVert Number of vertical pads.
   /// \param padding Padding between pads.
   /// \returns vector of vector (ret[x][y]) of created pads.
   std::vector<std::vector<TPad *>> Divide(int nHoriz, int nVert, const TPadPos &padding = {});

   /// Add something to be painted.
   /// The pad observes what's lifetime through a weak pointer.
   template <class T>
   auto &Draw(const std::shared_ptr<T> &what)
   {
      // Requires GetDrawable(what) to be known!
      return AddDrawable(GetDrawable(what));
   }

   /// Add something to be painted, with options.
   /// The pad observes what's lifetime through a weak pointer.
   template <class T, class OPTIONS>
   auto &Draw(const std::shared_ptr<T> &what, const OPTIONS &options)
   {
      // Requires GetDrawable(what, options) to be known!
      return AddDrawable(GetDrawable(what, options));
   }

   /// Add something to be painted. The pad claims ownership.
   template <class T>
   auto &Draw(std::unique_ptr<T> &&what)
   {
      // Requires GetDrawable(what) to be known!
      return AddDrawable(GetDrawable(std::move(what)));
   }

   /// Add something to be painted, with options. The pad claims ownership.
   template <class T, class OPTIONS>
   auto &Draw(std::unique_ptr<T> &&what, const OPTIONS &options)
   {
      // Requires GetDrawable(what, options) to be known!
      return AddDrawable(GetDrawable(std::move(what), options));
   }

   /// Add a copy of something to be painted.
   template <class T, class = typename std::enable_if<!ROOT::TypeTraits::IsSmartOrDumbPtr<T>::value>::type>
   auto &Draw(const T &what)
   {
      // Requires GetDrawable(what) to be known!
      return Draw(std::make_unique<T>(what));
   }

   /// Add a copy of something to be painted, with options.
   template <class T, class OPTIONS,
             class = typename std::enable_if<!ROOT::TypeTraits::IsSmartOrDumbPtr<T>::value>::type>
   auto &Draw(const T &what, const OPTIONS &options)
   {
      // Requires GetDrawable(what, options) to be known!
      return Draw(std::make_unique<T>(what), options);
   }

   /// Remove an object from the list of primitives.
   // TODO: void Wipe();

   /// Get the elements contained in the canvas.
   const Primitives_t &GetPrimitives() const { return fPrimitives; }

   /// Get the size of the pad.
   const TPadPos &GetSize() const { return fSize; }

   /// Convert a `Pixel` position to Canvas-normalized positions.
   const TPadCoord PixelsToNormal(const TPadCoord &pos) const {
      if (!fParent) {
         assert(!fSize.fHoriz.fNorm && !fSize.fVert.fNorm && !fSize.fHoriz.fUser && !fSize.fHoriz.fUser "Canvas size must be in pixels!");
         assert(!fSize.f && !fSize.fUser && "Canvas size must be in pixels!");
         return {pos.fPixels[0] / fSize.fHoriz.fPixel, pos[1] / fSize.fVert.fPixel};
      }
      // Normalized coords given the parent size:
      std::array<TPadCoord::Normal, 2> parentNormal = fParent->ToNormal({pos[0], pos[1]});

      // Our size is fSize; need to know in parent's Normal:
      Normal mySizeInParentNormal = ;
      return {parentNormal[0] / fParent->ToNormal(fSize).}
   }

   /// Convert a TPadPos to [x, y] of normalized coordinates.
   std::array<TPadCoord::Normal, 2> ToNormal(const TPadPos &pos) const {
      std::array<TPadCoord::Normal, 2> pixelsInNormal = PixelsToNormal({coord.fHoriz.fPixel, coord.fVert.fPixel});
      return {pos.fHoriz.fNormal + pixelsInNormal[0] + 
   }
   /// Convert a TPadPos to of normalized coordinates.
   
};

} // namespace Experimental
} // namespace ROOT

#endif
