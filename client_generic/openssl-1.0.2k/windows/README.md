# OpenSSL 1.1.1w - built with Visual Studio 2022 CE, MinGW-w64 (cross)

## Visual Studio (with VS cmd, unpack the archive with Windows 10 tar)

### 32bit

(optionally apply `openssl-1.1-crypto-init-win9x.diff`)

You may need to disable `/WX` in `Configurations/10-main.conf` before running Configure,
just remove the line:

```perl
CFLAGS           => add("/WX"),
```

then

```bat
perl Configure VC-WIN32 no-shared no-pinshared no-dynamic-engine no-capieng no-async --prefix=C:\Work\Clamav\openssl\win32\build
```

Add `-D_WIN32_WINNT=0x0501` for backward compatibility.


```bat
:: Optional for multicore build
set CL=/MP

nmake install_dev
```


### 64bit
```bat
perl Configure VC-WIN64A no-shared no-pinshared no-dynamic-engine no-capieng no-async --prefix=C:\Work\Clamav\openssl\x64\build
```

```bat
:: Optional for multicore build
set CL=/MP

nmake install_dev
```


## MinGW (Currently cross-compiled on Linux)

(optionally apply `openssl-1.1-crypto-init-win9x.diff` and `openssl-1.1-mingw-wspiapi.diff`)

### x86

```sh
./Configure mingw no-shared no-pinshared no-dynamic-engine no-capieng no-async --cross-compile-prefix=i686-w64-mingw32- --prefix=`pwd`/../dist
make -jX
make install_dev
```

### x86\_x64
```sh
./Configure mingw64 no-shared no-pinshared no-dynamic-engine no-capieng no-async --cross-compile-prefix=x86_64-w64-mingw32- --prefix=`pwd`/../dist64
make -jX
make install_dev
```
