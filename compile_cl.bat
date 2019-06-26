@echo off

call .\find_mvsc.bat

if NOT DEFINED VCINSTALLDIR (
    echo "No Visual Studio installation found, aborting, try running run vcvarsall.bat first!"
    exit 1
)

IF NOT EXIST build (
    mkdir build
)

pushd build

cl.exe /nologo /Zi /O2 /D_CRT_SECURE_NO_WARNINGS /D_HAS_EXCEPTIONS=0 /EHsc /W3 /wd5045 /wd4514 /wd4710 /wd4820 /wd4820 /wd4668 /wd4464 /wd5039 /wd4255 /wd4626 /wd4711 ..\test\test.cpp ..\test\main.cpp ..\third-party\nadir\src\nadir.cpp ..\third-party\lizard\lib\*.c ..\third-party\lizard\lib\entropy\*.c ..\third-party\lizard\lib\xxhash\*.c /link  /out:test.exe /pdb:test.pdb

popd

exit /B %ERRORLEVEL%
