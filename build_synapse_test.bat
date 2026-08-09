@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d "C:\Users\PCMyPC\Downloads\jarvis-github-project2\jarvis\build"
C:\Qt\Tools\Ninja\ninja.exe jarvis > "%~dp0build_synapse_test.log" 2>&1
