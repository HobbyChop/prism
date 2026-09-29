# PRISM

A sound module for the PS Vita. It plays SoundFont banks as a sixteen part
General MIDI module and, with your own ROM images, emulates the Roland MT-32
through Munt. MIDI comes in over USB from the PSP-MIDI adapter.

## What it does

- Sixteen parts, each with its own MIDI channel, patch, level, pan, cutoff,
  resonance, attack, release, key range and transpose. Layers and splits are
  parts sharing a channel.
- Any SoundFont 2 bank placed in ux0:data/prism/soundfonts. GeneralUser GS
  is built in. Banks that will not fit in memory, SF3 files and files that
  are not SoundFonts are marked in red in the list with the reason.
- An effects rack: reverb and chorus with a send per part, a three band EQ
  and a limiter on the master.
- MT-32 mode using the Munt emulator. Put MT32_CONTROL.ROM and MT32_PCM.ROM,
  or the CM-32L pair, in ux0:data/prism/mt32. Split dumps are merged. The
  emulator renders its partials on all three cores; the changes are in
  munt/prism-parallel.patch and every one of them is bit exact, with a
  self check at start-up.
- States. A state is the whole module: the bank, every part, the rack, the
  master, the polyphony and the MT-32 settings. Sixteen user slots, named on
  the device. Launch asks whether to reload the last state.
- USB recovery after sleep, a failed port takeover or a pulled cable, and a
  manual reconnect by holding SELECT.
- An audio ring that renders ahead of the port by a chosen number of blocks,
  so a slow block does not reach the speaker as a gap.

## Requirements

- A PS Vita or PS TV with HENkaku or h-encore, unsafe homebrew enabled. The
  MIDI driver is a kernel module and loads from the app's own folder.
- The PSP-MIDI adapter on the USB port.

## SoundFonts

Put SoundFont files (.sf2) in ux0:data/prism/soundfonts. They appear in the
list on SETUP the next time the page opens. A bank is loaded whole into
memory, so the size limit is the free memory at launch less about 4 MB for
the loader: the boot screen prints it as "Memory: N MB free for banks" and
SETUP shows it as ROOM FOR A BANK OF N MB. Banks over that are listed in red.
The built-in GeneralUser GS is 32 MB. SF3 files, which compress their
samples, are not supported.

## MT-32 ROMs

PRISM does not include the MT-32 ROMs. To use MT-32 mode you need your own
dumps of the control and PCM ROMs of an MT-32 or CM-32L you own:
MT32_CONTROL.ROM and MT32_PCM.ROM, or the CM-32L pair. Put them in
ux0:data/prism/mt32. Without them the MT-32 page says NO ROMS LOADED and
the mode stays off. GM mode needs nothing extra.

## Install

Install prism.vpk with VitaShell. The first launch creates ux0:data/prism
with the soundfonts, mt32 and performances folders.

## Controls

| Button | Action |
|---|---|
| D-pad | move: up and down along a column, left and right between columns and cards, on every page |
| O held with the d-pad | change the value under the cursor |
| L / R, or the tab bar | change page |
| O tap | PERFORM: load the state under the cursor |
| Triangle | PLAY: the patch list. EFFECTS: next part |
| X | audition; SETUP: load the bank; MT-32 page: act on the row |
| Square | PLAY and MIX: mute. PERFORM: save the state |
| Select | PERFORM: name the slot. Hold one second anywhere: reconnect USB |
| Start | all sound off |

The touch screen works everywhere: tap a row, drag a slider, tap the tabs.

## Building

Needs vitasdk with taihen on Linux or WSL, CMake, and Python 3 with Pillow
if the fonts are regenerated.

    ./build.sh

The script builds the Munt library from munt/mt32emu into munt/build, then
the app from prism/ into prism/build/prism.vpk. The kernel module and its
user shim are shipped as binaries in prism/module.

## Layout

    prism/          the app: panel, engine, effects, MT-32 wrapper, platform
    prism/tsf/      TinySoundFont with the local changes (tools/patch_tsf.py)
    prism/fonts/    IBM Plex atlases and their sources; tools/mkfont.py makes them
    prism/sf/       GeneralUser GS
    prism/module/   the MIDI kernel module and user shim, prebuilt
    prism/credits/  credits and licence texts, installed with the app
    prism/sce_sys/  LiveArea icon, background and gate; tools/mkart.py draws them
    munt/           Munt 2.8.2 library source with the changes, and the patch

## Licences

Munt (libmt32emu) is LGPL 2.1 and is statically linked; this repository
carries the modified library source and the patch against the upstream
release so it can be rebuilt and relinked. TinySoundFont is MIT. GeneralUser
GS is distributed under its own licence in prism/sf/LICENSE.txt. The IBM
Plex fonts are under the SIL Open Font License in prism/fonts/src/OFL.txt.
