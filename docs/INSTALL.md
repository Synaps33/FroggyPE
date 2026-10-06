# Installing FroggyPE

- Everything below is verified against this repository and a working GB300 card.
- Commands are for a Linux or macOS machine with the card mounted. Windows: use
  WSL, or drag the files in a file manager; the layout is what matters, not the tool.

## What you need

- The FroggyPE core, from the release:
  <https://github.com/Synaps33/FroggyPE/releases/tag/v1.0.0>
  - Download `mcpe.sf2k` **or** `core_87000000`. They are byte-identical.
- The game assets, from this repository, in the `data/` folder at the repository root
  - About 3.7 MB in total: `app`, `fonts`, `images`, `lang`, `sound`
- The FrogUI firmware, `bisrv.asd`, only if your card does not already have it
  - It ships with a FrogUI build, and `sf2000_multicore` regenerates it on every build
  - If `bios/bisrv.asd` is already on your card, keep it and ignore this step

## Where the files go

| Source | Destination on the card | Required |
|---|---|---|
| `mcpe.sf2k` | `system/Deimos/cores/mcpe.sf2k` | yes |
| `core_87000000` | `cores/mcpe/core_87000000` | alternative to the line above |
| empty file | `ROMS/mcpe/Minecraft PE` | yes |
| `data/` from the repo | `mcpe/data/` | yes |
| `bisrv.asd` | `bios/bisrv.asd` | once, if missing |

- **Both core names are the same file.** The project's own install rule copies
  `core_87000000` to `system/Deimos/cores/mcpe.sf2k`. Pick whichever your FrogUI build
  expects; `mcpe.sf2k` is what the standard build installs.
- **`ROMS/mcpe/Minecraft PE` must be a 0-byte file.** FrogUI maps the folder name `mcpe`
  to this core, so the file only has to exist. The core ignores its contents and boots
  into its own title screen. If the folder is empty, the game will not appear in FrogUI.

## Copying it over

- Assume the card is mounted at `/media/USER/GB300`; substitute your own path.
- Plug the card in and confirm it mounted before copying:

```bash
# check the card is mounted and look right
ls /media/USER/GB300
```

- Create the folders and copy the core:

```bash
CARD=/media/USER/GB300

mkdir -p "$CARD/system/Deimos/cores"
cp mcpe.sf2k "$CARD/system/Deimos/cores/mcpe.sf2k"

# or, for the other layout
mkdir -p "$CARD/cores/mcpe"
cp core_87000000 "$CARD/cores/mcpe/core_87000000"
```

- Create the placeholder ROM:

```bash
mkdir -p "$CARD/ROMS/mcpe"
: > "$CARD/ROMS/mcpe/Minecraft PE"      # the colon makes an empty file
```

- Copy the assets:

```bash
mkdir -p "$CARD/mcpe"
cp -r data "$CARD/mcpe/"
```

- Add the firmware, only if the card has none:

```bash
mkdir -p "$CARD/bios"
cp bisrv.asd "$CARD/bios/bisrv.asd"
```

- Flush writes before unplugging. Removing the card early is how files get truncated:

```bash
sync
```

## Verifying the copy

- The core should be **8756192 bytes**, MD5 `d887b48ed84ebd9a6b50d9e605c2f1c7`:

```bash
md5sum "$CARD/system/Deimos/cores/mcpe.sf2k"
ls -l "$CARD/system/Deimos/cores/mcpe.sf2k"
```

- A smaller size means the transfer was cut short. Copy it again.
- The placeholder must be empty:

```bash
ls -l "$CARD/ROMS/mcpe/Minecraft PE"     # expect 0 bytes
```

- The assets should be about 3.7 MB:

```bash
du -sh "$CARD/mcpe/data"
```

## Launching

- Boot the console into FrogUI
- D-Pad to the folder `mcpe` under ROMs
- Select `Minecraft PE` and press **A**
- The game opens on its own title screen

## Where your worlds live

- Not on the SD card. Worlds and `options.txt` are written to the **console's internal
  storage**, so they survive taking the card out.
- Path in use, in order of preference:
  - `/mnt/sda1/mcpe/`
  - `/mnt/sda1/bios/`
  - a relative `mcpe/`
- Layout: `games/com.mojang/minecraftWorlds/<world>/` and `options.txt`
- The firmware has no `rename()`, so `level.dat` is committed by copying bytes and a
  `level.dat_new` file is left behind too. `level.dat` is the one that gets read.
- **Overwriting `options.txt` resets every setting**, including GUI scale and render
  distance.

## Trapping the player back up

- The console's memory is shared between the firmware and the core. If the console
  becomes unstable, freezes on boot, or shows a blue screen, the core may be asking for
  too much of it.
- The heap limit lives in the third-party framework, not in this repository:
  `sf2000_multicore/src/libretro_frontend/lib.c`
- Lower it from `0x03A00000` (58 MiB) back to `0x03400000` (52 MiB) and rebuild.
- A blue screen at the moment of a crash, rather than a frozen frame, points at heap
  exhaustion: `sbrk` returns -1 and the framework shows a BSOD. Check `froggy.log` on
  the card for `sbrk OOM`.

## Removing it

- Delete the core from `system/Deimos/cores/` and `cores/mcpe/`
- Delete `ROMS/mcpe/` and `mcpe/`
- Worlds and settings on the console are untouched unless you delete
  `/mnt/sda1/mcpe/`