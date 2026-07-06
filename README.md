# Echelon: 3D "vertical" scrolling shooter toy for Arcadia

Echelon is a scrolling shooter rendered with OpenGL, and the first
[Arcadia][] toy not developed by Synthetic Reality. It supports co-op
multiplayer, and players can pop in and out of the game at any time.

![](docs/screenshot.png)

## Build

This toy was developed using the [Arcadia SDK][]. Building requires
nothing more than this source and [w64devkit][] (x86 version):

    $ cmake -B build
    $ cmake --build build

It automatically downloads the SDK. Copy `build/toys/toy13/` into your
Arcadia `toys/` directory to install the toy. The toy run on Windows XP,
but building it requires at least Windows 7 due to CMake.


[Arcadia]: http://www.synthetic-reality.com/arcadia.htm
[Arcadia SDK]: https://github.com/skeeto/arcadia-sdk
[w64devkit]: https://github.com/skeeto/w64devkit
