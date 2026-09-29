@echo off
setlocal

REM ============================================================
REM  SMART_HOME one-click flash  (ALIENTEK DAP / CMSIS-DAP)
REM  Target: STM32F429IGTx on ALIENTEK Apollo V2 baseboard
REM  Flashes the NEWEST SMART_HOME.elf found in Debug\ or build\.
REM  (ASCII-only on purpose: cmd.exe reads .bat as GBK)
REM ============================================================

set "OCD=D:\STM32CubeIDE\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.openocd.win32_2.4.500.202604080855\tools\bin\openocd.exe"
set "SCR=D:\STM32CubeIDE\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.debug.openocd_2.3.400.202606220929\resources\openocd\st_scripts"

set "ELF="
for /f "usebackq delims=" %%F in (`powershell -NoProfile -Command "$f=Get-ChildItem -LiteralPath '%~dp0Debug','%~dp0build' -Filter SMART_HOME.elf -ErrorAction SilentlyContinue ^| Sort-Object LastWriteTime ^| Select-Object -Last 1; if($f){$f.FullName}"`) do set "ELF=%%F"

if not defined ELF (
  echo *** No SMART_HOME.elf found - build the project first ***
  pause
  exit /b 1
)

set "ELF_TCL=%ELF:\=/%"

echo.
echo ============================================================
echo  Flashing %ELF%
echo ============================================================
echo.

taskkill /f /im openocd.exe >nul 2>&1

"%OCD%" -s "%SCR%" -f interface/cmsis-dap.cfg -f target/stm32f4x.cfg ^
        -c "init" ^
        -c "program %ELF_TCL% verify reset" ^
        -c "shutdown"

echo.
if errorlevel 1 (echo *** FLASH FAILED ***) else (echo *** FLASH OK - target reset and running ***)
echo.
pause
