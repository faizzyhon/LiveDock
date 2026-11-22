@echo off
rem Push this version as an UPGRADE to the existing repo faizzyhon/LiveDock (uses YOUR logged-in GitHub: gh).
cd /d "%~dp0"
where gh >nul 2>nul || (echo Install GitHub CLI first: winget install GitHub.cli ^& then: gh auth login & pause & exit /b)
set REPO=faizzyhon/LiveDock
set WORK=%TEMP%\livedock_upgrade
rmdir /s /q "%WORK%" 2>nul
gh repo clone %REPO% "%WORK%" || (echo Repo not found. Run publish.bat first. & pause & exit /b)
robocopy "%~dp0." "%WORK%" /E /XD .git >nul
if errorlevel 8 (echo Copy failed. & pause & exit /b)
cd /d "%WORK%"
git add -A
git commit -m "LiveDock 1.1.0: liquid glass dock, Windows 11 24H2 wallpaper fix, new Studio look"
git push
gh release create v1.1.0 LiveDock.exe --repo %REPO% --title "LiveDock 1.1.0" --notes-file CHANGELOG.md
echo.
echo Done: https://github.com/%REPO%/releases/tag/v1.1.0
pause
