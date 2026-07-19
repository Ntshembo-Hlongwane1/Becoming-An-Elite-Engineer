# Codeforces Runner
# Usage: .\run.ps1 <problem_code>
# Example: .\run.ps1 71A

param(
    [Parameter(Mandatory=$true, Position=0)]
    [string]$Code
)

$SrcDir = Join-Path $PSScriptRoot $Code
$SrcFile = Join-Path $SrcDir "$Code.cpp"
$ExeFile = Join-Path $SrcDir "$Code.exe"

# Check if source file exists
if (-not (Test-Path $SrcFile)) {
    Write-Host "Error: $SrcFile not found!" -ForegroundColor Red
    exit 1
}

# Compile
Write-Host "Compiling $Code.cpp..." -ForegroundColor Cyan
g++ -std=c++17 -O2 -Wall -o $ExeFile $SrcFile

if ($LASTEXITCODE -ne 0) {
    Write-Host "Compilation failed!" -ForegroundColor Red
    exit 1
}

# Run
Write-Host "Running $Code..." -ForegroundColor Green
Write-Host "-------------------"
& $ExeFile
