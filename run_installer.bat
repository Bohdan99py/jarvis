@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;%PATH%
cd /d "C:\Users\PCMyPC\Downloads\jarvis-github-project2\jarvis"
call scripts\build_release.bat "C:\Qt\6.11.1\msvc2022_64" --installer > build_installer_out.log 2>&1
