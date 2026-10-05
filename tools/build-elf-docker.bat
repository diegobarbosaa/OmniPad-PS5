@echo off
rem OmniPad PS5 — Docker Build Script for Windows
set PROJ_DIR=%~dp0..

echo [*] Construindo imagem Docker OmniPad-Build...
docker build -t omnipad-build "%PROJ_DIR%"

if "%~1"=="" (
    echo [*] Compiling ELF with built-in PS5 Payload SDK...
    docker run --rm -v "%PROJ_DIR%":/work omnipad-build make ps5
) else (
    echo [*] Compiling ELF with local SDK at %~1...
    docker run --rm -v "%~1":/opt/ps5-payload-sdk -v "%PROJ_DIR%":/work -e PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk omnipad-build make ps5
)

echo [+] Concluido! Verifique a pasta dist/
