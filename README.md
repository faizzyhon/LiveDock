<p align="center"><img src="assets/livedock.png" width="120" alt="LiveDock"></p>

<h1 align="center">LiveDock</h1>
<p align="center"><b>Live wallpaper + transparent dock for Windows.</b><br>
Tiny. Light on RAM and CPU. Made for low-end PCs.</p>
<p align="center"><i>A side vibe-coded project by <a href="https://github.com/faizzyhon">faizzyhon</a></i></p>

---

## What is it for?

Windows has a boring taskbar and a still wallpaper. LiveDock changes both, without eating your PC:

- **Live wallpaper** - a calm built-in animation, or your own GIF / image. You can pick a **video** and it turns it into a high-resolution GIF for you.
- **Transparent dock** - a floating rounded dock replaces the Windows taskbar. Start button, running apps, pin your favourite apps.
- **Very low use** - one small `.exe` (about 260 KB), no runtime to install, low CPU priority.
- **Auto start** - starts with Windows by itself.

## Why it is light

- Native C++ (Win32 + GDI+), no .NET, no Electron, no browser engine
- Runs at below-normal priority, so your apps always win
- Wallpaper animation runs at 10 fps (GIF uses its own speed)
- Wallpaper pauses and the dock hides when a game or fullscreen app is open
- The dock only redraws when something changes

## Use it

1. Download `LiveDock.exe` from [Releases](https://github.com/faizzyhon/LiveDock/releases).
2. Run it. The Windows taskbar hides and the dock appears. Auto start turns on.
3. Right-click the dock or the tray icon for the menu:

| Menu item | What it does |
|---|---|
| Choose wallpaper | Pick a GIF or image |
| Choose video | Makes a GIF from your video and sets it as wallpaper |
| High resolution GIF (1920) | Untick for a lighter 1280 GIF |
| Use built-in animation | Back to the default scene |
| Start with Windows | Turn auto start on or off |
| Pin / Unpin (right-click an app) | Keep an app on the dock |
| **Exit** | Brings the Windows taskbar back |

You can also drop a file named `wallpaper.gif` (or `.png` / `.jpg`) next to `LiveDock.exe`.
Settings are saved in `livedock.ini` next to the exe.

### Video to GIF
The first time, LiveDock downloads `ffmpeg` (about 30 MB) next to the exe. After that it works offline.
It uses the first 10 seconds of the video (`GifSeconds=` in `livedock.ini`).
A big GIF uses more RAM and CPU. On a very slow PC, untick the high resolution option.

## Build it yourself

```sh
sudo apt install mingw-w64     # Linux or WSL
./build.sh                     # makes LiveDock.exe
```

## Honest notes

- This is a **side vibe-coded project** built with Claude. It works for me, but I have not tested every Windows version.
- Newest Windows 11 builds change how the desktop is built. If the wallpaper does not show, open an issue.
- The exe is not code-signed, so Windows SmartScreen may warn. Click "More info" then "Run anyway", or build it yourself.
- If it ever crashes and the taskbar stays hidden, restart `explorer.exe` (Task Manager) and it comes back.

## About me

**Muhammad Faizan (faizzyhon)** - Full Stack Developer and AI Engineer, Madinah, Saudi Arabia.

- GitHub: [github.com/faizzyhon](https://github.com/faizzyhon)
- Website: [faizzyhon.com](https://faizzyhon.com)
- Instagram: [instagram.com/faizzyhon](https://instagram.com/faizzyhon)
- Email: faizzyhon@gmail.com

If you like it, give the repo a star.

## License

MIT - see [LICENSE](LICENSE).
