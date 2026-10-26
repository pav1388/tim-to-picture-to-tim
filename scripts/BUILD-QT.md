# Qt 5.6.3 (static, MinGW 4.9.2 32-bit) — build notes

Reference notes for reproducing the static Qt SDK used by `ttptt`.
The result is the `qt-static-5.6.3/` folder sitting next to `CMakeLists.txt`
(git-ignored). `scripts/build.bat` expects exactly this name.

## Toolchain

- **MinGW 4.9.2 (32-bit)** — the official toolchain Qt 5.6.3 was released with.
  Any later 32-bit MinGW (5.3, 7.3, 8.1) that is C++11-capable should also work,
  but 4.9.2 is the reference used to build this project.
- The `mingw492_32\bin` directory must be **first** in `PATH` (otherwise
  `gcc` / `mingw32-make` may resolve to another MinGW installation).
- `scripts/build.bat` prints `where gcc` and `gcc --version` before building —
  check that it reports `4.9.2` and points to `mingw492_32\bin\gcc.exe`.
- Python 2.x is required by Qt 5.6's `configure.bat`.

## Requirements

- Qt sources: `qt-src-5.6.3` (from download.qt.io/archive/qt/5.6/5.6.3/single/).
- A separate, empty build directory: `qt-build-5.6.3`.
- MinGW 4.9.2 (32-bit) in `PATH`.

## Configure

Run from the **build directory** (not the source directory):

    R:\qt-src-5.6.3\configure.bat -release -static -target xp ^
        -platform win32-g++ -opensource -confirm-license ^
        -prefix R:\qt-static-5.6.3 ^
        -opengl desktop ^
        -no-sse4.1 -no-sse4.2 -no-avx -no-avx2 ^
        -qt-zlib -qt-pcre -qt-libpng -qt-libjpeg -qt-freetype ^
        -make libs ^
        -nomake tools -nomake examples -nomake tests ^
        -skip qt3d -skip qtactiveqt -skip qtandroidextras ^
        -skip qtconnectivity -skip qtdeclarative -skip qtdoc ^
        -skip qtlocation -skip qtmultimedia -skip qtscript ^
        -skip qtsensors -skip qtsvg -skip qtwayland ^
        -skip qtwebchannel -skip qtwebengine -skip qtwebview

Key flags:

- `-static`         — static linking (the exe has no Qt DLL dependencies).
- `-target xp`      — keeps the binary compatible with Windows XP / SP3.
- `-opengl desktop` — system `opengl32.dll` instead of ANGLE.
- `-no-sse4.1 -no-sse4.2 -no-avx -no-avx2`
                    — 32-bit-safe baseline for old CPUs; also reduces size.
                      MinGW 4.9.2 defaults to an SSE2 baseline, these flags
                      pin the target for the whole Qt build.
- `-qt-zlib -qt-pcre -qt-libpng -qt-libjpeg -qt-freetype`
                    — bundle the third-party libs into the static archive
                      (avoids relying on anything in the toolchain root).
- `-make libs`      — build only the libraries needed by `ttptt`
                      (Core, Gui, Widgets and their deps).
- `-nomake tools|examples|tests`
                    — do not build tools/examples/tests.
- `-skip ...`       — skip modules ttptt does not use.

## Build and install

    mingw32-make -j4
    mingw32-make install

`install` copies everything into `R:\qt-static-5.6.3\` (`-prefix` above).
After that, move/copy the folder to the project root as `qt-static-5.6.3/`
(or point `TTPTT_QT_DIR` / `-DCMAKE_PREFIX_PATH` at it in `build.bat`).

## Cleaning

From the **build directory**:

    mingw32-make clean       :: recompiles from scratch on next build
    mingw32-make distclean   :: also removes the Makefile and cache

## What gets linked into ttptt

The CMakeLists links these static components explicitly, because Qt 5.6's
static packages do not register them via `find_package`:

    libqtpcre.a
    libqtharfbuzzng.a
    plugins/platforms/libqwindows.a
    plugins/imageformats/libqico.a
    libQt5PlatformSupport.a
    libqtfreetype.a

If a component is absent (e.g. `libqtfreetype.a` when Qt was built without
`-qt-freetype`), `link_static_qt_prv` simply skips it and prints a note.

## Notes on licensing

The resulting binary is statically linked against a GPL-licensed build of Qt
(`-opensource`), which is why `ttptt` itself is distributed under GPL-3.0.
See `LICENSE` in the repository root.