# Boost Gateway v3.3.2 -- Gateway Server Windows Docker image
#
# Multi-stage build producing a minimal Windows nanoserver image
# containing the gateway-server and tank-battle-demo executables.
#
# Build:
#   docker build -f docker/gateway-server.Dockerfile `
#       --build-arg BUILD_TYPE=Release `
#       -t gateway-server:latest .
#
# Run:
#   docker run -p 8080:8080 gateway-server:latest
#
# Prerequisites:
#   - Docker Desktop for Windows (Windows container mode)
#   - OR a remote Windows build node with Docker

# ============================================================================
# Stage 1: Build
# ============================================================================
FROM mcr.microsoft.com/windows/servercore:ltsc2022 AS builder

ARG BUILD_TYPE=Release

SHELL ["powershell", "-Command", "$ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue';"]

# Install Visual Studio Build Tools 2022 with C++ workload
ADD https://aka.ms/vs/17/release/vs_BuildTools.exe vs_BuildTools.exe
RUN Start-Process -Wait -NoNewWindow -FilePath .\\vs_BuildTools.exe `
    --add Microsoft.VisualStudio.Workload.VCTools `
    --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    --add Microsoft.VisualStudio.Component.Windows10SDK.20348 `
    --includeRecommended `
    --quiet --wait --norestart --nocache

# Install CMake
ADD https://github.com/Kitware/CMake/releases/download/v3.30.5/cmake-3.30.5-windows-x86_64.zip cmake.zip
RUN Expand-Archive -Path cmake.zip -DestinationPath C:\cmake; `
    Remove-Item cmake.zip -Force

# Install Git
ADD https://github.com/git-for-windows/git/releases/download/v2.47.1.windows.1/Git-2.47.1-64-bit.exe git.exe
RUN Start-Process -Wait -NoNewWindow -FilePath C:\git.exe `
    /VERYSILENT /NORESTART /DIR=C:\git; `
    Remove-Item git.exe -Force

# Set environment variables for MSVC
RUN $env:PATH = \"C:\\cmake\\cmake-3.30.5-windows-x86_64\\bin;C:\\git\\cmd;${env:PATH}\"

WORKDIR C:\src

# Copy entire repository
COPY . .

# Configure and build
RUN cmd.exe /c \" \
    \"C:\\Program Files\\Microsoft Visual Studio\\2022\\BuildTools\\Common7\\Tools\\VsDevCmd.bat\" \
    && set PATH=C:\\cmake\\cmake-3.30.5-windows-x86_64\\bin;%PATH% \
    && cmake -B build -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -G \"Ninja\" -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl \
    && cmake --build build --config %BUILD_TYPE% --target tank_battle_demo --parallel \
    && cmake --build build --config %BUILD_TYPE% --target gateway_server --parallel \
    \"

RUN $buildType = $env:BUILD_TYPE.ToLower(); \
    if (-not (Test-Path \"C:\\src\\build\\bin\\${buildType}\")) { New-Item -ItemType Directory -Path \"C:\\src\\build\\bin\\${buildType}\" -Force }; \
    Get-ChildItem -Path \"C:\\src\\build\" -Recurse -Filter \"tank_battle_demo.exe\" | ForEach-Object { Copy-Item $_.FullName \"C:\\src\\build\\bin\\${buildType}\" }; \
    Get-ChildItem -Path \"C:\\src\\build\" -Recurse -Filter \"gateway_server.exe\" | ForEach-Object { Copy-Item $_.FullName \"C:\\src\\build\\bin\\${buildType}\" }

# ============================================================================
# Stage 2: Runtime
# ============================================================================
FROM mcr.microsoft.com/windows/nanoserver:ltsc2022 AS runtime

ARG BUILD_TYPE=Release

SHELL ["cmd", "/S", "/C"]

WORKDIR C:\app

# Copy build artifacts
COPY --from=builder C:\src\build\bin\%BUILD_TYPE%\*.exe C:\app\bin\
COPY --from=builder C:\src\build\bin\%BUILD_TYPE%\*.dll C:\app\bin\
COPY --from=builder C:\src\config C:\app\config
COPY --from=builder C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC\*.exe C:\vc_redist.exe

# Install VC++ redistributables
RUN C:\vc_redist.exe /quiet /norestart && del C:\vc_redist.exe

# Copy runtime DLLs from VC redist directory
COPY --from=builder "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT\*.dll" C:\app\bin\

EXPOSE 8080
EXPOSE 50051

ENTRYPOINT ["C:\\app\\bin\\tank_battle_demo.exe"]
CMD ["--port=8080"]
