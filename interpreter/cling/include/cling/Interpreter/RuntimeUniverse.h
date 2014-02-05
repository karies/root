//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Vassil Vassilev <vasil.georgiev.vasilev@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------
#ifndef CLING_RUNTIME_UNIVERSE_H
#define CLING_RUNTIME_UNIVERSE_H

#if !defined(__CLING__)
#error "This file must not be included by compiled programs."
#endif

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS // needed by System/DataTypes.h
#endif

#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS // needed by System/DataTypes.h
#endif

#ifdef __cplusplus

#ifdef _LIBCPP_EXTERN_TEMPLATE
#undef _LIBCPP_EXTERN_TEMPLATE
#endif
#define _LIBCPP_EXTERN_TEMPLATE(...)

#include "cling/Interpreter/RuntimeException.h"

namespace llvm {
  class GenericValue;
}

namespace cling {

  class Interpreter;

  /// \brief Used to stores the declarations, which are going to be
  /// available only at runtime. These are cling runtime builtins
  namespace runtime {

    /// \brief The interpreter provides itself as a builtin, i.e. it
    /// interprets itself. This is particularly important for implementing
    /// the dynamic scopes and the runtime bindings
    extern Interpreter* gCling;

    /// \brief The function is used to deal with null pointer dereference.
    /// It receives input from a user and decides to proceed or not by the
    /// input.
    bool shouldProceed(void* S, void* T);

    namespace internal {
      ///\brief Manually provided by cling missing function resolution using
      /// addSymbol()
      ///
      /// Implemented in Interpreter.cpp
      ///
      int local_cxa_atexit(void (*func) (void*), void* arg, void* dso, 
                           void* interp);

      /// \brief Some of clang's routines rely on valid source locations and
      /// source ranges. This member can be looked up and source locations and
      /// ranges can be passed in as parameters to these routines.
      ///
      /// Use instead of SourceLocation() and SourceRange(). This might help,
      /// when clang emits diagnostics on artificially inserted AST node.
      int InterpreterGeneratedCodeDiagnosticsMaybeIncorrect;

      ///\brief Allocate the StoredValueRef and return the GenericValue
      /// for an expression evaluated at the prompt.
      ///
      /// Implemented in ExecutionContext.cpp
      ///
      ///\param [in] - The cling::Interpreter to allocate the SToredValueRef.
      ///\param [in] - The opaque ptr for the clang::QualType of value stored.
      ///\param [out] - The StoredValueRef that is allocated.
      llvm::GenericValue& allocateValueAndGetGV(Interpreter& interp, void* vpQT,
                                                void* vpStoredValRef);

      ///\brief Set the value of the GenericValue for the expression
      /// evaluated at the prompt.
      ///\param [in] - The cling::Interpreter to allocate the SToredValueRef.
      ///\param [in] - The opaque ptr for the clang::QualType of value stored.
      ///\param [in] - The float value of the assignment to be stored
      ///              in GenericValue.
      ///\param [out] - The StoredValueRef that is created.
      void setValue(Interpreter& interp,  void* vpQT, float value,
                    void* vpStoredValRef);

      ///\brief Set the value of the GenericValue for the expression
      /// evaluated at the prompt.
      ///\param [in] - The cling::Interpreter to allocate the SToredValueRef.
      ///\param [in] - The opaque ptr for the clang::QualType of value stored.
      ///\param [in] - The double value of the assignment to be stored
      ///              in GenericValue.
      ///\param [out] - The StoredValueRef that is created.
      void setValue(Interpreter& interp,  void* vpQT, double value,
                    void* vpStoredValRef);

      ///\brief Set the value of the GenericValue for the expression
      /// evaluated at the prompt.
      ///\param [in] - The cling::Interpreter to allocate the SToredValueRef.
      ///\param [in] - The opaque ptr for the clang::QualType of value stored.
      ///\param [in] - The uint64_t value of the assignment to be stored
      ///              in GenericValue.
      ///\param [out] - The StoredValueRef that is created.
      void setValue(Interpreter& interp,  void* vpQT, unsigned long long value,
                    void* vpStoredValRef);

      ///\brief Set the value of the GenericValue for the expression
      /// evaluated at the prompt.
      ///\param [in] - The cling::Interpreter to allocate the SToredValueRef.
      ///\param [in] - The opaque ptr for the clang::QualType of value stored.
      ///\param [in] - The int64_t value of the assignment to be stored
      ///              in GenericValue.
      ///\param [out] - The StoredValueRef that is created.
      void setValue(Interpreter& interp,  void* vpQT, long long value,
                    void* vpStoredValRef);

      ///\brief Set the value of the GenericValue for the expression
      /// evaluated at the prompt.
      ///\param [in] - The cling::Interpreter to allocate the SToredValueRef.
      ///\param [in] - The opaque ptr for the clang::QualType of value stored.
      ///\param [in] - The pointer value of the assignment to be stored
      ///              in GenericValue.
      ///\param [out] - The StoredValueRef that is created.
      void setValue(Interpreter& interp,  void* vpQT, void* value,
                    void* vpStoredValRef);

      ///\brief Set the value of the GenericValue for the expression
      /// evaluated at the prompt.
      ///\param [in] - The cling::Interpreter to allocate the SToredValueRef.
      ///\param [in] - The opaque ptr for the clang::QualType of value stored.
      ///\param [in] - The object value of the assignment to be stored
      ///              in GenericValue.
      ///\param [out] - The StoredValueRef that is created.
      template<typename T>
      void setValue(Interpreter& interp,  void* vpQT, const T& value,
                    void* vpStoredValRef) {
        setValue(interp, vpQT, (const void*)&value, vpStoredValRef);
      }


//__cxa_atexit is declared later for WIN32
#if (!_WIN32)
      // Force the module to define __cxa_atexit, we need it.
      struct __trigger__cxa_atexit {
        ~__trigger__cxa_atexit(); // implemented in Interpreter.cpp
      } S;
#endif

    } // end namespace internal
  } // end namespace runtime
} // end namespace cling

using namespace cling::runtime;

// Global d'tors only for C++:
#if _WIN32
extern "C" {

  ///\brief Fake definition to avoid compilation missing function in windows
  /// environment it wont ever be called
  void __dso_handle(){}
  //Fake definition to avoid compilation missing function in windows environment
  //it wont ever be called
  int __cxa_atexit(void (*func) (), void* arg, void* dso) {
    return 0;
  }
}
#endif

extern "C" {
  int cling_cxa_atexit(void (*func) (void*), void* arg, void* dso) {
    return cling::runtime::internal::local_cxa_atexit(func, arg, dso,
                                                 (void*)cling::runtime::gCling);
  }

  ///\Brief a function that throws NullDerefException. This allows to 'hide' the
  /// definition of the exceptions from the RuntimeUniverse and allows us to 
  /// run cling in -no-rtti mode. 
  /// 
  void cling__runtime__internal__throwNullDerefException(void* Sema, 
                                                         void* Expr);

}
#endif // __cplusplus

#endif // CLING_RUNTIME_UNIVERSE_H
