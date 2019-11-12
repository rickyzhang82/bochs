make clean
./configure --with-x --with-X11 --enable-all-optimizations --enable-idle-hack --enable-readline --enable-plugins --enable-cpu-level=3 --enable-fpu --enable-fast-function-calls --enable-cpp --enable-iodebug --enable-x86-debugger --enable-ne2000 --disable-mmx

make -j 16
