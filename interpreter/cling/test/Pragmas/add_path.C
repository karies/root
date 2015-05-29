//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: mkdir -p %T/subdir && clang -shared %S/call_lib.c -o %T/subdir/libtest%shlibext
// RUN: cd %T/subdir && %S/create_header.sh && cd ..
// RUN: cat %s | %cling -I %S -Xclang -verify 2>&1 | FileCheck %s

#pragma cling add_include_path("subdir")
#pragma cling load("Include_header.h")
include_test()
// CHECK: OK(int) 0

#pragma cling add_library_path("subdir")
#pragma cling load("libtest")

// RUN: rm -rf subdir
//expected-no-diagnostics
.q
