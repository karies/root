#!/bin/sh
#
# Build a single large pcm for the entire basic set of ROOT libraries.
# Script takes as optional argument the source directory path.
#
# Copyright (c) 2013 Rene Brun and Fons Rademakers
# Author: Fons Rademakers, 19/2/2013

srcdir=$1
shift
objdir=$1
shift
pchfile=$1
shift
modules=$1
shift
allHeaders=allHeaders.h
allLinkdefs=allLinkDef.h

rm -f $allHeaders $allLinkdefs cppflags.txt
# previous versions put these into include/
rm -f $objdir/include/allHeaders.h $objdir/include/allLinkDef.h

while ! [ "x$1" = "x" ]; do
    echo '#include "'$1'"' >> $allHeaders
    shift
done

for dict in `cd $objdir; find $modules -name 'G__*.cxx' 2> /dev/null | grep -v /G__Cling.cxx  | grep -v core/metautils/src/G__std_`; do
    dirname=`dirname $dict`                   # to get foo/src
    dirname=`echo $dirname | sed -e 's,/src$,,' -e 's,^[.]/,,' ` # to get foo/

    case $dirname in
        graf2d/qt | math/fftw | math/foam | math/fumili | math/mlp | math/quadp | math/splot | math/unuran | math/vc | math/vdt) continue;;

        interpreter/* | core/* | io/io | net/net | math/* | hist/* | tree/* | graf2d/* | graf3d/gl | gui/gui | gui/fitpanel | rootx | bindings/pyroot | roofit/* | tmva | main) ;;

        *) continue;;
    esac

    # Check if selmodules already contains the dirname.
    # Happens for instance for math/smatrix with its two (32bit and 64 bit)
    # dictionaries.
    if ! ( echo $selmodules | grep "$dirname " > /dev/null ); then
        selmodules="$selmodules$dirname "
    fi

    awk 'BEGIN{START=-1} /includePaths\[\] = {/, /^0$/ { if (START==-1) START=NR; else if ($0 != "0") { sub(/",/,"",$0); sub(/^"/,"-I",$0); print $0 } }' $objdir/$dict >> cppflags.txt
    echo "// $dict" >> $allHeaders
#     awk 'BEGIN{START=-1} /payloadCode =/, /^;$/ { if (START==-1) START=NR; else if ($1 != ";") { code=substr($0,2); sub(/\\n"/,"",code); print code } }' $dict >> $allHeaders
    awk 'BEGIN{START=-1} /headers\[\] = {/, /^0$/ { if (START==-1) START=NR; else if ($0 != "0") { sub(/,/,"",$0); print "#include",$0 } }' $objdir/$dict >> $allHeaders

    if ! test "$dirname" = "`echo $dirname| sed 's,/qt,,'`"; then
        # something qt; undef emit afterwards
        cat <<EOF >> $allHeaders
#ifdef emit
# undef emit
#endif
#ifdef signals
# undef signals
#endif
EOF
    elif ! test "$dirname" = "`echo $dirname| sed 's,net/ldap,,'`"; then
        # ldap; undef Debug afterwards
        cat <<EOF >> $allHeaders
#ifdef Debug
# undef Debug
#endif
#ifdef GSL_SUCCESS
# undef GSL_SUCCESS
#endif
EOF
    fi

    find $srcdir/$dirname/inc/ -name '*LinkDef*.h' | \
        sed -e 's|^|#include "|' -e 's|$|"|' >> $allLinkdefs
done

echo
echo Generating the one large pcm for $selmodules, patience...
echo

# check if rootcling_tmp exists in the expected location (not the case for CMake builds)
if [ ! -x $objdir/core/utils/src/rootcling_tmp ]; then
  exit 0
fi

cxxflags="-D__CLING__ -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -I$objdir/include -I$objdir/etc -I$objdir/etc/cling `cat cppflags.txt | sort | uniq`"
rm cppflags.txt

# generate one large pcm
rm -f allDict.* lib/allDict_rdict.pc*
touch allDict.cxx.h
$objdir/core/utils/src/rootcling_tmp -1 -f allDict.cxx -noDictSelection -c $cxxflags $allHeaders $allLinkdefs
res=$?
if [ $res -eq 0 ] ; then
  mv allDict_rdict.pch $pchfile
  res=$?

  # actually we won't need the allDict.[h,cxx] files
  #rm -f allDict.*
fi

exit $res
