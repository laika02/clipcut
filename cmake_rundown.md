# CMake Rundown

## What It Is

`CMake` is a cross-platform build generator. You do not usually compile your app directly with `cmake`. Instead, you describe the project once in `CMakeLists.txt`, and CMake generates the right native build files for the platform:

- Linux: `Makefiles` or `Ninja`
- Windows: `Visual Studio` solution files or `Ninja`
- macOS: `Xcode` or `Ninja`

For this rewrite, that matters because Linux and Windows are both first-line targets.

## Why I’m Leaning Toward It

- It is the standard pragmatic choice for portable C/C++ projects.
- It makes SDL2 and FFmpeg dependency discovery much cleaner than hand-written platform-specific makefiles.
- It gives us one project definition instead of separate Linux and Windows build logic.
- It scales better once we add tests, third-party libraries, and platform conditionals.

## What Using It Looks Like

Typical workflow:

```bash
cmake -S C_Rewrite -B C_Rewrite/build
cmake --build C_Rewrite/build
```

That means:

- source lives in `C_Rewrite`
- generated build files live in `C_Rewrite/build`
- the source tree stays clean

## What The Project File Does

A `CMakeLists.txt` file would define things like:

- project name
- C standard version
- source files
- include directories
- compiler warnings
- dependency lookup for SDL2 and FFmpeg
- output executable name

## Pros

- Best cross-platform path for Linux + Windows together
- Easy out-of-tree builds
- Good editor and IDE support
- Cleaner dependency handling
- Future-proof for tests and packaging

## Cons

- One more tool to learn
- The syntax is not especially elegant
- Dependency discovery can still be annoying if libraries are installed inconsistently

## Simpler Alternative

The simpler alternative is a plain `Makefile`.

That is fine if:

- we only care about Linux for a while
- we want the smallest possible bootstrap

That is weaker if:

- Windows is a first-line target
- we want one maintained build path instead of separate platform logic

## Recommendation

For this project, I recommend `CMake`.

Reason:

The rewrite is explicitly cross-platform, and Linux + Windows are already first-line requirements. That pushes this out of "simple local Makefile" territory and into "one portable build definition" territory.
