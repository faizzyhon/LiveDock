@echo off
rem One-click publish. Uses YOUR logged-in GitHub (gh). Needs: git + GitHub CLI (winget install GitHub.cli), then "gh auth login" once.
cd /d "%~dp0"
where gh >nul 2>nul || (echo Install GitHub CLI first: winget install GitHub.cli & pause & exit /b)
git init -b main
git add .
git commit -m "LiveDock: live wallpaper + transparent dock by faizzyhon"
gh repo create faizzyhon/LiveDock --public --source . --remote origin --push --description "Tiny live wallpaper + transparent dock for Windows. Low RAM/CPU. Side vibe-coded project by faizzyhon."
gh repo edit faizzyhon/LiveDock --add-topic windows --add-topic live-wallpaper --add-topic dock --add-topic taskbar --add-topic cpp --add-topic win32 --add-topic low-end-pc
gh release create v1.0.0 LiveDock.exe --title "LiveDock 1.0.0" --notes "First release. Live wallpaper (GIF/image/video to GIF) + transparent dock. By faizzyhon."
echo Done: https://github.com/faizzyhon/LiveDock
pause
