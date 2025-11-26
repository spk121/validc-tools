@echo off
setlocal enabledelayedexpansion
set FAILED=0

rem Build editor if not present
if not exist "..\led_exe.exe" (
  echo led_exe not found. Please run `make led_exe`.
  set FAILED=1
  goto :end
)

rem Test 1: Empty script should exit 0 on EOF
"..\led_exe.exe" -S scripts\empty.ed > scripts\empty.out.txt 2>&1
if not !ERRORLEVEL!==0 (
  echo FAIL: empty script exit code !ERRORLEVEL!
  set FAILED=1
) else (
  echo PASS: empty script exit code
)

rem Test 2: Write file via script and compare contents
if exist scripts\actual_write_basic.txt del /f /q scripts\actual_write_basic.txt
"..\led_exe.exe" -S scripts\write_basic.ed > scripts\write_basic.out.txt 2>&1
if not !ERRORLEVEL!==0 (
  echo FAIL: write_basic script exit code !ERRORLEVEL!
  set FAILED=1
) else (
  echo PASS: write_basic script exit code
)

fc /W scripts\expected_write_basic.txt scripts\actual_write_basic.txt > nul 2>&1
if not !ERRORLEVEL!==0 (
  echo FAIL: output file mismatch for write_basic
  set FAILED=1
) else (
  echo PASS: output file match for write_basic
)

rem Test 3: Global command with brace-enclosed multi-command list
if exist scripts\actual_global_brace.txt del /f /q scripts\actual_global_brace.txt
"..\led_exe.exe" -S scripts\global_brace.ed > scripts\actual_global_brace.txt 2>&1
if not !ERRORLEVEL!==0 (
  echo FAIL: global_brace script exit code !ERRORLEVEL!
  set FAILED=1
) else (
  echo PASS: global_brace script exit code
)

fc /W scripts\expected_global_brace.txt scripts\actual_global_brace.txt > nul 2>&1
if not !ERRORLEVEL!==0 (
  echo FAIL: output mismatch for global_brace
  set FAILED=1
) else (
  echo PASS: output match for global_brace
)

rem Test 4: Inverse command with brace-enclosed multi-command list
if exist scripts\actual_inverse_brace.txt del /f /q scripts\actual_inverse_brace.txt
"..\led_exe.exe" -S scripts\inverse_brace.ed > scripts\actual_inverse_brace.txt 2>&1
if not !ERRORLEVEL!==0 (
  echo FAIL: inverse_brace script exit code !ERRORLEVEL!
  set FAILED=1
) else (
  echo PASS: inverse_brace script exit code
)

fc /W scripts\expected_inverse_brace.txt scripts\actual_inverse_brace.txt > nul 2>&1
if not !ERRORLEVEL!==0 (
  echo FAIL: output mismatch for inverse_brace
  set FAILED=1
) else (
  echo PASS: output match for inverse_brace
)

:end
exit /b !FAILED!
