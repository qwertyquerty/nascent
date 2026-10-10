# Nascent

> A vertical scrolling rhythm game passion project

### Project Goals

The goal is to write a fast, lightweight, hit accurate, and clean implementation of an N-lane VSRG with a play performance calculator, custom chart file format, an in-game chart editor, support for importing many filetypes from other VSRGs, online play submissions, global ranking system, cheat detection, and more.

This project is meant to fix a lot of the problems I have with osu!mania and Etterna, taking the best of both worlds and adding some of my own. However, it is just a passion project and is not likely to be a drop-in replacement for either one at any point in its development.

The project will mostly serve as a learning experience and a good waste of time for me and any others who choose to collaborate on it.

### Collaboration

In reality I don't really know what I'm doing with C++ projects of this scale as I have few that span the goals of this game. I would love any help from people who know more than me about proper C++ project structure, software architecture, and optimizations.

Currently you can message me @qwertyquerty on Discord if you're interested in helping out, and I'll eventually make a community Discord server if the project takes off.

### Build instructions

```sh
git clone --recurse-submodules https://github.com/qwertyquerty/nascent
cd nascent
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

#### Tests

```sh
ctest --test-dir build --output-on-failure
```

#### Formatting

Code style is defined in `.clang-format`. Format everything with `scripts/format.sh`

### Project layout

- `src/core` - engine independent gameplay logic including charts, hit judgement, scoring, the song clock
- `src/gse` - a small scene/entity layer on top of olcPixelGameEngine
- `src/game` - Nascent's scenes, rendering entities, skins, settings and input bindings
- `src/main` - the executable entry point
- `src/libs` - third party libraries, mostly git submodules (see `src/libs/README.md`)
- `tests` - unit tests for `src/core`
- `tools` - chart conversion and difficulty tuning scripts
- `server` - the game server API

### Credits

*Made with OLC PixelGameEngine onelonecoder.com*

#### Dependencies

- olcPixelGameEngine
  - olcPGEX_MiniAudio
  - olcPGEX_TTF
- miniaudio
- nlohmann/json
- FreeType
- KISS FFT
- discord-presence
- stb_image
- Open Arrow font
- picosha2
