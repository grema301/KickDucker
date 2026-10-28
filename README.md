# KickDucker

A sidechain-driven **dynamic notch EQ** for kick and 808 bass. KickDucker listens to the pitch of your kick on the sidechain input and carves a matching notch out of your bass — only while the kick is hitting — optionally with the 2nd, 3rd and 4th harmonics too.

![KickDucker](docs/screenshot.png)

## Download

**[Get the latest installer from Releases](../../releases/latest)** → download `KickDucker-x.y.z-Windows-Setup.exe` and run it.

- Windows 10 / 11, 64-bit
- VST3 format (installs to `C:\Program Files\Common Files\VST3`)
- Developed and tested in FL Studio; should work in any VST3 host with sidechain support
- Close your DAW before installing, then rescan plugins afterwards

> **Windows SmartScreen:** the installer isn't code-signed yet, so Windows may show "Windows protected your PC". Click **More info → Run anyway**.

## How it works

1. A YIN pitch detector tracks the fundamental (25–280 Hz) of the **sidechain** signal.
2. A peaking-EQ notch follows that frequency on your **main** signal.
3. An envelope follower on the sidechain controls how deep the notch is — it digs in when the kick hits and releases as it fades.

| Control | What it does |
|---|---|
| **Depth** | Maximum cut at the detected frequency |
| **Q** | Notch width (higher = narrower) |
| **Harmonics** | Also duck 2×/3×/4× the fundamental |
| **Smoothing** | How slowly the notch glides to a new pitch |
| **Attack / Release** | How fast the notch engages / lets go |
| **Snap** | Snap the detected pitch to the nearest note |
| **Lock** | Freeze the notch at its current frequency |
| **Mix / Out Gain** | Dry/wet blend and make-up gain |

Tip: drag the ring on the display to set Depth, scroll over the display to change Q.

## Setting up the sidechain

<!-- TODO: add your FL Studio routing steps (and any other DAW you've tested) here -->

## Building from source

Requirements: [JUCE](https://juce.com) 7 or 8, Visual Studio 2022 (Windows).

1. Open `KickDucker.jucer` in the Projucer and click **Save Project and Open in IDE**
2. Build the **Release | x64** configuration (Release should use the static runtime so users don't need the VC++ redistributable)
3. To make the installer: install [Inno Setup](https://jrsoftware.org/isinfo.php) 6.3+ and run `installer\build_installer.bat`

## Licence

See [`installer/EULA.txt`](installer/EULA.txt). Built with [JUCE](https://juce.com). VST is a registered trademark of Steinberg Media Technologies GmbH.
