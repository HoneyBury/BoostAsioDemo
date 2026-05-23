@echo off
REM Local build helper — adds include_override for locked runtime.h workaround (Win Insider 26200 bug)
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="/I %~dp0include_override" || exit /b 1
cmake --build build --config Release %*
