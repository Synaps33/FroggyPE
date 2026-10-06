# FroggyPE

- Minecraft Pocket Edition **0.6.1 alpha**, playable on the **GB300 / SF2000** handheld.
- Runs as a FroggyPE core through the Deimos frontend on the console's stock firmware.
- 3D is rendered by a **software rasterizer**: this hardware has no GPU, and the stock
  firmware provides no `rename()`, `unlink()` or `getcwd()`.

## Install

The release ships the core under **two names**. They are **byte-identical**; pick the one
your FrogUI build expects.

- `mcpe.sf2k` — what the standard build installs, at `system/Deimos/cores/mcpe.sf2k`
- `core_87000000` — the raw multicore core name, for layouts that keep cores in a
  per-console folder at `cores/mcpe/core_87000000`
- The project's own install rule copies `core_87000000` to
  `sdcard/GB300V2/system/Deimos/cores/mcpe.sf2k`, which is why both names appear

### Steps

- Copy the core, choosing one of these destinations:
  - `system/Deimos/cores/mcpe.sf2k`, or
  - `cores/mcpe/core_87000000`
- Create an empty placeholder file at `ROMS/mcpe/Minecraft PE`
  - FrogUI maps the folder name `mcpe` to this core, so the file only has to exist
  - The core ignores its contents and boots into its own title screen
- `bios/bisrv.asd` is required once, taken from a FrogUI build
- Optional: copy `data/lang/en_US.lang` to `mcpe/data/lang/en_US.lang` on the card for
  translated UI strings

### Verify the copy

- Both files should be `8756360` bytes with MD5 `651e7431daf11daee0b4413486444c27`
- If the size differs, the copy was truncated

### Launch

- Boot the console into FrogUI
- D-Pad to the folder `mcpe`
- Select `Minecraft PE` and press **A**

## Save data

- Worlds and `options.txt` live on the **console's internal storage**, not on the SD card
- Path in use: `/mnt/sda1/mcpe/`, falling back to `/mnt/sda1/bios`, then a relative `mcpe/`
- Layout: `games/com.mojang/minecraftWorlds/<world>/`, plus `options.txt`
- Overwriting `options.txt` resets every setting
- The firmware has no `rename()`, so `level.dat` is committed by copying bytes and a
  `level.dat_new` file is also left behind; `level.dat` is the file that gets read

## Controls

### In game

- D-Pad up/down — walk forward / back
- D-Pad left/right — change hotbar slot
- **R** (right shoulder) — use block / place / open
- **L** (left shoulder) — break block
- **A / B / Y / X** — look right / down / left / up
- **SELECT** — inventory and block palette
- **SELECT + X** — crafting table (2x2)
- **SELECT + Y** — armour screen
- **SELECT + D-Pad up** — inventory, when Auto Jump is off
- **SELECT + D-Pad down** — drop held item
- **START** — pause menu

### In menus

- Navigation depends on the screen, and both styles are used:
  - **Cursor** — inventory and options
    - D-Pad steps a pointer, 12 px per press, with auto-repeat
    - **A** presses the nearest control
  - **Arrows** — crafting and the world carousel
    - D-Pad steps between controls, **A** presses the focused one
    - Left/Right on a focused value control changes the value in place
- **B** / **START** — back / close
- Auto Jump on: **SELECT** becomes inventory; Auto Jump off: **SELECT** becomes jump

### Blocks that open a screen

- Crafting table, furnace, chest — all open on **R**
- Crafting lists only recipes whose ingredients are held; the rest are hidden
- The D-Pad scrolls the recipe list, including rows below the fold

## Options

- GUI Scale — `100%`, `115%`, `130%`, `150%`, or Auto
  - Auto resolves to **120%** on this panel
  - Drawn through an orthographic projection in logical units, so the scale is a
    straight element-size multiplier
- Render Distance — 8 steps, `Shortest` (1 chunk) to `Far` (26 chunks)
  - The chunk grid follows the player by wrap-around
- Block Resolution — `1/4` (80x60), `1/2` (160x120), `3/4` (240x180), `1/1` (320x240)
- Sound — Music and Sound have their own category
- Auto Jump, difficulty, sensitivity, third person, and the rest

## Performance

- Measured on the target hardware class: **frame cost scales with pixel count**, not with
  chunk count. 2 chunks and 26 chunks differ far less than 80x60 and 320x240 do.
- **Resolution is the only lever that reliably changes the frame rate.** Lower Block
  Resolution costs less; it is the dominant term.
- A fast path was added to the software rasterizer's span loop for opaque, fully lit,
  power-of-two textures, removing per-pixel tests of span-invariant flags.
  This is logically sound but **could not be measured**: repeated runs of one identical
  configuration on the build machine varied by 2x, so the change is unproven.
- An incremental texel-walk replacing a per-pixel modulo was written and then **reverted**:
  it was measured that the modulo path is never taken (all textures are powers of two),
  so it only added a per-pixel branch and texel-drift risk.

## Build

### PC, for testing

- `make -f Makefile.pc -j$(nproc)`
- Produces `mcpe_pc_test`, a headless test runner
- Runs with `--headless` plus one of the modes below

### MIPS, for the console

- `make -f Makefile.sf2000 clean && make -f Makefile.sf2000 -j$(nproc)`
- Produces `mcpe_libretro_sf2000.a`
- The core itself is built by the multicore framework:
  `make FROGGY_TYPE=GB300V2 CONSOLE="mcpe" CORE=cores/mcpe`
- Produces `build/core_87000000`, installed as `mcpe.sf2k`

### Test modes

- `--storage-test` — level.dat commit, options persistence, unique world directories
- `--input-test` — movement, hotbar, Auto Jump, screen switching, special blocks,
  recipe filtering, world carousel
- `--test-options` — options screen navigation
- `--render-test` — chunk grid follows the player at every render distance
- `--ui-res N` — GUI scale, cursor stepping, option hit areas
- `--auto-world` — full menu-to-game flow
- `--soak-test` — long run with resident-memory sampling
- `--perf-test` — frame-time ablation across resolution and distance

- Tests drive the real input path and assert against live widget rectangles rather than
  hard-coded pixels.
- Regression guards were confirmed to **fail** when their fix is reverted, so a passing
  run is meaningful.

## Known bugs

### Not diagnosed

- **Game freezes when a creeper explodes.**
  - The explosion itself is small and is not the cause by the numbers:
    creeper radius is `2.4`, TNT `3.1`, the ray march runs 8-14 steps, and
    `toBlow` holds roughly 30-60 blocks, not thousands
  - `NO_NETWORK` is defined for SF2000, so the client-side `ExplodePacket` path never
    runs on the console; the explosion is processed once, server-side, in-process
  - Open questions: frozen image or black screen? Does the player respawn?
- **Chunk rebuild backlog during longer play.**
  - The soak test measured flat resident memory over 4000 in-game frames, so this is not
    a memory leak.
  - The soak test's player never actually walked, so no chunks were streamed and the
    rebuild path was never exercised. This remains the most likely explanation for a
    freeze that appears after some minutes of walking.

### Found, not fixed

- **`containerMenu` dangles after closing a furnace or chest.**
  - `FurnaceScreen` and `ChestScreen` call `closeContainer()` only from `buttonClicked`,
    so closing via Back or START leaves the pointer set.
  - `Player::closeContainer()` nulls the pointer but **never deletes the menu**, so every
    open leaks it.
  - `Player::tileEntityDestroyed` can then dereference it and crash. This was hit in a
    test that removed a block while a container menu still referenced its tile entity.
- **`Level::getTile` has no x/z bounds check.**
  - The guard at `Level.cpp:516-518` is commented out, and `getChunk` generates missing
    chunks.
  - Any code path that walks far in x or z can generate world indefinitely.
  - Latent rather than active: the explosion's own loop is bounded.
- **Armour cannot be equipped from the inventory list with the D-Pad.**
  - `ArmorScreen` exposes only the 4 armour slots and Close as focus targets; the pane's
    item list is not part of navigation.
  - Unequipping works. Equipping through the pane is unverified.
- **Heap limit raised from 52 MiB to 58 MiB, untested on the device.**
  - Lives in `sf2000_multicore/src/libretro_frontend/lib.c`, which is a third-party
    framework and not part of this repository.
  - The 64 MiB scratch buffer is shared with the firmware and OS, so 6 MiB of headroom is
    left. If the console becomes unstable, set `SF2000_HEAP_LIMIT` back to `0x03400000`
    and rebuild.

### Unproven changes

- The recipe-list scroll clamp is logically correct, but the test passes without it too.

## Credits

- Original Minecraft Pocket Edition 0.6.1 work by **Kolyah35** —
  https://gitea.sffempire.ru/Kolyah35/minecraft-pe-0.6.1
- Forked from **mschiller890/mcpe64** (https://github.com/mschiller890/mcpe64)
- GB300/SF2000 core framework: **sf2000_multicore** (ISC), by kobily, bnister,
  tzubertowski and contributors
- Console frontend: **FrogUI**