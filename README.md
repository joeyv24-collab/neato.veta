# Neato: Saltreach

A PS Vita homebrew open-world superhero game. Neato survives an accident in the
flooded, quarantined island district of Saltreach and comes out able to absorb
kinetic energy: every fall, sprint and hit charges her up.

## Controls

| Button | Action |
| --- | --- |
| Left stick | Move |
| Right stick | Camera |
| R (hold) | Sprint. Sprint into a building to run up the wall |
| X | Jump. Hold X in the air to glide. X on a wall kicks off it |
| O | Dodge roll. In the air: ground slam (stronger with more charge) |
| □ | Strike: dashes to the nearest guard and chains between them |
| △ | Counter a guard who's flashing, otherwise kinetic blast (costs charge) |
| L (hold) | Kinetic sense: see guards through walls |
| Start | Pause / controls |
| Start + Select | Quit |

Falling into the flood water puts you back on the last solid ground.

**Goal:** break the six Vantage Security checkpoints (orange light beams in the fog, yellow on the minimap).
Unaware guards can be taken down silently from behind or by dropping on them from above.

## Getting the .vpk

Every push builds a new release: open the repo's **Releases** page and download `neato.vpk`.

## Building the .vpk

Every push to GitHub builds `neato.vpk` with GitHub Actions. Open the **Actions** tab,
pick the latest run, and download **neato-vpk** from Artifacts.

To build it locally you need [VitaSDK](https://vitasdk.org) plus vitaGL, vitaShaRK, math-neon and
SceShaccCgExt (the workflow file shows the exact steps), then:

```
mkdir build && cd build && cmake .. && make
```

## Installing on the Vita

1. Copy `neato.vpk` to the Vita (VitaShell → USB or FTP) and install it.
2. The renderer compiles its shaders on the console, so the Vita needs
   `ur0:data/libshacccg.suprx`. Most modded Vitas already have it. If yours doesn't, install
   it with the ShaRKBR33D homebrew.

## Project layout

- `src/`: game code (C99)
  - `game.c`: player movement, camera, effects, HUD
  - `world.c`: generated city district, collision
  - `model.c` / `anim.c`: skinned model loading and code-driven animation
  - `gfx_gl.c`: vitaGL renderer · `gfx_soft.c`: software renderer for desktop tests
- `assets_src/`: original model and textures · `assets/`: converted game files
- `tools/fbx2nmdl.py`: converts the FBX model and PNG textures (`make -f Makefile.test assets`)
- `tests/`: desktop test harness: `make -f Makefile.test && ./neato_test`
