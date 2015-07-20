setlocal
@echo off
@call "c:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\bin\vcvars32.bat"
cl srch.cpp /nologo /EHsc /MT
