#!/usr/bin/env sh
set -evx

chmod +x ci/get-nprocessors.sh
. ci/get-nprocessors.sh

# If possible, ask for the precise number of processors,
# otherwise take 2 processors as reasonable default; see
# https://docs.travis-ci.com/user/speeding-up-the-build/#Makefile-optimization
if [ -x /usr/bin/getconf ]; then
  NPROCESSORS=$(/usr/bin/getconf _NPROCESSORS_ONLN)
else
  NPROCESSORS=2
fi

# As of 2017-09-04 Travis CI reports 32 processors, but GCC build
# crashes if parallelized too much (maybe memory consumption problem),
# so limit to 4 processors for the time being.
if [ $NPROCESSORS -gt 4 ]; then
  echo "$0:Note: Limiting processors to use by make from $NPROCESSORS to 4."
  NPROCESSORS=4
fi

# Tell make to use the processors. No preceding '-' required.
MAKEFLAGS="j${NPROCESSORS}"
export MAKEFLAGS

env | sort

# Set default values to OFF for these variables if not specified.
: "${NO_EXCEPTION:=OFF}"
: "${NO_RTTI:=OFF}"
: "${COMPILER_IS_GNUCXX:=OFF}"

# Install dependency from YUM
yum install -y libasan
yum install -y systemd-devel
yum install -y tsg_master-devel
yum install -y framework_env
yum install -y mrzcpd
yum install -y libfieldstat3-devel
yum install -y libmaat4-devel
yum install -y libswarmkv-devel
yum install -y libMESA_handle_logger-devel
yum install -y libMESA_prof_load-devel
yum install -y sapp-devel # Maat_rule require MESA/stream.h
yum install -y numactl-libs # required by mrzcpd
yum install -y libibverbs # required by mrzcpd
yum install -y libbreakpad_mini-devel
yum install -y msgpack-devel
source /etc/profile.d/framework.sh
source /etc/profile.d/mrzcpd.sh


if [ $ASAN_OPTION ]; then
  source /opt/rh/devtoolset-7/enable
fi

mkdir build || true
cd build

cmake3 -DCMAKE_CXX_FLAGS=$CXX_FLAGS \
  -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
  -DASAN_OPTION=$ASAN_OPTION \
  -DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX \
  -DVERSION_DAILY_BUILD=$TESTING_VERSION_BUILD \
  ..
make

if [ -n "${PACKAGE}" ]; then
  make package
  cp ~/rpm_upload_tools.py ./
  python3 rpm_upload_tools.py ${PULP3_REPO_NAME} ${PULP3_DIST_NAME} *.rpm
fi

