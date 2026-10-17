#!/usr/bin/env bash
# Cross-compile airShot on Linux with the x86_64 Haiku cross tools and the
# libraries of the workstation's Haiku build (/mnt/HaikuWork/x399/build).
# Produces build-cross/airShot (with resources) and build-cross/airShot_filter.
# Usage: tools/build-cross.sh [make-target]
set -euo pipefail
AIRSHOT_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
HAIKU_SRC=${HAIKU_SRC:-/mnt/HaikuWork/src/haiku}
HAIKU_BUILD=${HAIKU_BUILD:-/mnt/HaikuWork/x399/build/x86_64}
CROSS="$HAIKU_BUILD/cross-tools-x86_64/bin/x86_64-unknown-haiku"
OBJECTS="$HAIKU_BUILD/objects/haiku/x86_64/release"
SYSLIBS=$(ls -d "$HAIKU_BUILD"/build_packages/gcc_syslibs_devel-*-x86_64 | head -1)
# The devel package's .so links point into the runtime package.
RUNTIME=$(ls -d "$HAIKU_BUILD"/build_packages/gcc_syslibs-*-x86_64 | head -1)
CXXLIBS="$RUNTIME/lib/libstdc++.so.6 $RUNTIME/lib/libgcc_s.so.1 $HAIKU_BUILD/cross-tools-x86_64/lib/gcc/x86_64-unknown-haiku/13.3.0/libgcc.a"
HOST_TOOLS="$HAIKU_BUILD/objects/linux/x86_64/release/tools"

# The C++ library headers come first: they reach the C headers with
# #include_next, which only searches the directories listed after them.
INCLUDES="-isystem $SYSLIBS/develop/headers/c++ -isystem $SYSLIBS/develop/headers/c++/x86_64-unknown-haiku"
INCLUDES+=" -isystem $SYSLIBS/develop/headers/gcc/include"
INCLUDES+=" -isystem $HAIKU_SRC/headers -isystem $HAIKU_SRC/headers/posix"
for dir in "$HAIKU_SRC"/headers/os "$HAIKU_SRC"/headers/os/*/ "$HAIKU_SRC"/headers/os/add-ons/*/; do
	[[ -d $dir ]] && INCLUDES+=" -isystem ${dir%/}"
done
INCLUDES+=" -isystem $HAIKU_SRC/headers/private/interface"
INCLUDES+=" -isystem $HAIKU_SRC/headers/private/shared"
INCLUDES+=" -isystem $HAIKU_SRC/headers/private/app"
INCLUDES+=" -isystem $HAIKU_SRC/headers/private/storage"
INCLUDES+=" -isystem $HAIKU_SRC/headers/private"
INCLUDES+=" -isystem $HAIKU_SRC/headers/glibc"

GLUE="$OBJECTS/system/glue"
CRT="$HAIKU_BUILD/cross-tools-x86_64/lib/gcc/x86_64-unknown-haiku/13.3.0"
LIBDIRS="-L$OBJECTS/kits -L$OBJECTS/kits/game -L$OBJECTS/kits/tracker -L$OBJECTS/kits/translation \
	-L$OBJECTS/kits/shared -L$OBJECTS/system/libroot"
START="-nodefaultlibs -nostartfiles $GLUE/arch/x86_64/crti.o $CRT/crtbeginS.o $GLUE/start_dyn.o $GLUE/init_term_dyn.o"
END="$CRT/crtendS.o $GLUE/arch/x86_64/crtn.o"
SHARED_START="-nodefaultlibs -nostartfiles $GLUE/arch/x86_64/crti.o $CRT/crtbeginS.o $GLUE/init_term_dyn.o"

cd "$AIRSHOT_ROOT"
exec make BUILD=build-cross \
	CXX="$CROSS-g++" \
	CROSS_CPPFLAGS="-nostdinc $INCLUDES -D__HAIKU__" \
	APP_CPPFLAGS="" FILTER_CPPFLAGS="" \
	APP_LDFLAGS="$START" APP_LDEND="$END $LIBDIRS -lroot $CXXLIBS -Xlinker --no-undefined" \
	FILTER_LDFLAGS="$SHARED_START" FILTER_LDEND="$END $LIBDIRS -lroot $CXXLIBS" \
	RC="$HOST_TOOLS/rc/rc" XRES="$HOST_TOOLS/xres" MIMESET="$HOST_TOOLS/mimeset --mimedb $OBJECTS/kits/libbe.so_mimedb" \
	"${1:-all}"
