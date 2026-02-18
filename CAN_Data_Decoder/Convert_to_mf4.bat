@echo off
pushd "%~dp0"

set PYEXE=python
rem If python on PATH fails, fall back to common install path
where "%PYEXE%" >nul 2>&1
if errorlevel 1 (
    set "PYEXE=%LocalAppData%\Programs\Python\Python313\python.exe"
)

"%PYEXE%" "%~dp0convert_to_mf4.py" --gui

popd
