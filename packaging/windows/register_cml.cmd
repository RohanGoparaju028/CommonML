@echo off
REM Associate .cml files with CommonML runtime on Windows and add bin directory to user PATH
set "BIN_DIR=%~dp0..\bin"
reg add "HKCU\Software\Classes\.cml" /ve /t REG_SZ /d "CommonML.SourceFile" /f >nul
reg add "HKCU\Software\Classes\.cml" /v "Content Type" /t REG_SZ /d "text/x-commonml" /f >nul
reg add "HKCU\Software\Classes\CommonML.SourceFile" /ve /t REG_SZ /d "CommonML Source File" /f >nul
reg add "HKCU\Software\Classes\CommonML.SourceFile\shell\open\command" /ve /t REG_SZ /d "\"%BIN_DIR%\cml.exe\" run \"%%1\"" /f >nul
reg add "HKCU\Software\Classes\.cmlbc" /ve /t REG_SZ /d "CommonML.Bytecode" /f >nul
reg add "HKCU\Software\Classes\CommonML.Bytecode\shell\open\command" /ve /t REG_SZ /d "\"%BIN_DIR%\commonml.exe\" --run-bc \"%%1\"" /f >nul
echo Associated .cml and .cmlbc files with CommonML.
