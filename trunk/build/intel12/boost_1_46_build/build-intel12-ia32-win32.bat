@echo off

rem -------------------------------------
set INTEL12_DIR=C:\Program Files (x86)\Intel\ComposerXE-2011
rem -------------------------------------

rem -------------------------------------
echo Setting Intel C++ Compiler 12 directory to %INTEL12_DIR%
rem -------------------------------------

call "%INTEL12_DIR%\bin\ICLVars.bat" ia32
if errorlevel 1 goto exit

bjam.exe -j4 --toolset=intel-12 cxxflags="/Qstd=c++0x" debug release link=static runtime-link=static threading=multi stage --stagedir=stage32
bjam.exe -j4 --toolset=intel-12 cxxflags="/Qstd=c++0x" debug release link=static runtime-link=shared threading=multi stage --stagedir=stage32

:exit