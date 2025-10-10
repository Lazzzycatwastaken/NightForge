param(
    [switch]$AddToPath = $false,
    [switch]$SkipCompile = $false,
    [switch]$Silent = $false
)

$ErrorActionPreference = "Stop"
$ProgressPreference = 'SilentlyContinue'

# Colors for output
function Write-Info { param($msg) Write-Host "[INFO] $msg" -ForegroundColor Cyan }
function Write-Success { param($msg) Write-Host "[SUCCESS] $msg" -ForegroundColor Green }
function Write-Error { param($msg) Write-Host "[ERROR] $msg" -ForegroundColor Red }
function Write-Warning { param($msg) Write-Host "[WARNING] $msg" -ForegroundColor Yellow }

# Banner
Write-Host @"
╔═══════════════════════════════════════════════════════════╗
║                                                           ║
║              NightForge Windows Installer                 ║
║                                                           ║
║  This installer will:                                     ║
║  1. Check for required tools (CMake, Git, C++ compiler)   ║
║  2. Install missing dependencies via winget/chocolatey    ║
║  3. Compile NightForge                                    ║
║  4. Optionally add NightScript to your PATH               ║
║                                                           ║
╚═══════════════════════════════════════════════════════════╝
"@ -ForegroundColor Magenta

Write-Host ""

# Check if running as administrator
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Warning "Not running as administrator. Some operations may require elevation."
    if (-not $Silent) {
        $response = Read-Host "Do you want to restart as administrator? (Y/n)"
        if ($response -ne 'n' -and $response -ne 'N') {
            $scriptPath = $MyInvocation.MyCommand.Path
            $arguments = "-ExecutionPolicy Bypass -File `"$scriptPath`""
            if ($AddToPath) { $arguments += " -AddToPath" }
            if ($SkipCompile) { $arguments += " -SkipCompile" }
            Start-Process powershell -Verb RunAs -ArgumentList $arguments
            exit
        }
    }
}

# Get the directory where this script is located
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $ScriptDir "build"
$InstallDir = Join-Path $ScriptDir "install"

Write-Info "Installation directory: $ScriptDir"
Write-Host ""

# ============================================================================
# Step 1: Check for package managers
# ============================================================================
Write-Info "Step 1/5: Checking for package managers..."

$hasWinget = $false
$hasChoco = $false

try {
    $null = Get-Command winget -ErrorAction Stop
    $hasWinget = $true
    Write-Success "winget found"
} catch {
    Write-Warning "winget not found"
}

try {
    $null = Get-Command choco -ErrorAction Stop
    $hasChoco = $true
    Write-Success "Chocolatey found"
} catch {
    Write-Warning "Chocolatey not found"
}

if (-not $hasWinget -and -not $hasChoco) {
    Write-Warning "No package manager found. Attempting to install Chocolatey..."
    
    if (-not $isAdmin) {
        Write-Error "Administrator privileges required to install Chocolatey."
        Write-Host "Please run this script as administrator or install Chocolatey/winget manually."
        exit 1
    }
    
    try {
        Set-ExecutionPolicy Bypass -Scope Process -Force
        [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
        Invoke-Expression ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))
        $hasChoco = $true
        Write-Success "Chocolatey installed successfully"
    } catch {
        Write-Error "Failed to install Chocolatey: $_"
        exit 1
    }
}

Write-Host ""

# ============================================================================
# Step 2: Check and install dependencies
# ============================================================================
Write-Info "Step 2/5: Checking for required dependencies..."

$dependencies = @{
    "cmake" = @{
        "command" = "cmake"
        "name" = "CMake"
        "winget" = "Kitware.CMake"
        "choco" = "cmake"
        "versionCmd" = "cmake --version"
    }
    "git" = @{
        "command" = "git"
        "name" = "Git"
        "winget" = "Git.Git"
        "choco" = "git"
        "versionCmd" = "git --version"
    }
    "gcc" = @{
        "command" = "g++"
        "name" = "MinGW-w64 (C++ Compiler)"
        "winget" = $null  # winget doesn't have a good mingw package
        "choco" = "mingw"
        "versionCmd" = "g++ --version"
        "optional" = $true
        "customInstall" = $true
    }
    "msvc" = @{
        "command" = "cl"
        "name" = "Visual Studio Build Tools"
        "winget" = "Microsoft.VisualStudio.2022.BuildTools"
        "choco" = "visualstudio2022buildtools"
        "optional" = $true
        "skipAutoInstall" = $true  # Too large, user should install manually
    }
}

$missingDeps = @()
$hasCompiler = $false
$detectedMingwPath = $null

foreach ($dep in $dependencies.GetEnumerator()) {
    $depInfo = $dep.Value
    $depKey = $dep.Key
    
    Write-Host "Checking for $($depInfo.name)..." -NoNewline
    
    try {
        $cmdInfo = Get-Command $depInfo.command -ErrorAction Stop
        Write-Host " FOUND" -ForegroundColor Green
        
        if ($depKey -eq "gcc" -or $depKey -eq "msvc") {
            $hasCompiler = $true
            
            # If this is g++, save its location for later
            if ($depKey -eq "gcc" -and $cmdInfo.Source) {
                $detectedMingwPath = Split-Path -Parent $cmdInfo.Source
                Write-Host "  Location: $detectedMingwPath" -ForegroundColor DarkGray
            }
        }
        
        # Show version
        try {
            $version = Invoke-Expression $depInfo.versionCmd 2>&1 | Select-Object -First 1
            Write-Host "  Version: $version" -ForegroundColor DarkGray
        } catch {}
    } catch {
        Write-Host " NOT FOUND" -ForegroundColor Red
        
        if (-not $depInfo.optional) {
            $missingDeps += $depInfo
        } elseif ($depKey -eq "gcc" -or $depKey -eq "msvc") {
            # Don't add to missing if it's a compiler (we only need one)
        }
    }
}

if (-not $hasCompiler) {
    Write-Warning "No C++ compiler found!"
    $dependencies["gcc"].optional = $false
    $missingDeps += $dependencies["gcc"]
}

Write-Host ""

# Install missing dependencies
if ($missingDeps.Count -gt 0) {
    Write-Warning "Missing dependencies detected: $($missingDeps.name -join ', ')"
    
    if (-not $Silent) {
        $response = Read-Host "Do you want to install missing dependencies? (Y/n)"
        if ($response -eq 'n' -or $response -eq 'N') {
            Write-Error "Cannot proceed without required dependencies."
            exit 1
        }
    }
    
    foreach ($dep in $missingDeps) {
        if ($dep.skipAutoInstall) {
            Write-Warning "Skipping automatic installation of $($dep.name)"
            Write-Host "Please install manually from: https://visualstudio.microsoft.com/downloads/"
            continue
        }
        
        Write-Info "Installing $($dep.name)..."
        
        try {
            # Special handling for MinGW
            if ($dep.customInstall -and $dep.name -like "*MinGW*") {
                if ($hasChoco) {
                    Write-Host "Installing MinGW-w64 via Chocolatey (this may take a few minutes)..."
                    choco install mingw -y --force
                    
                    # Also ensure make is available
                    Write-Host "Ensuring mingw32-make is available..."
                    
                    # Refresh PATH
                    $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path", "User")
                    
                    # Verify installation
                    try {
                        $null = Get-Command g++ -ErrorAction Stop
                        Write-Success "MinGW-w64 installed successfully"
                    } catch {
                        Write-Warning "MinGW may require a system restart or manual PATH configuration"
                        Write-Host "MinGW is typically installed to: C:\ProgramData\chocolatey\lib\mingw\tools\install\mingw64\bin"
                    }
                } else {
                    Write-Warning "Cannot auto-install MinGW without Chocolatey"
                    Write-Host "Please install manually from: https://sourceforge.net/projects/mingw-w64/"
                    Write-Host "Or install Chocolatey first: https://chocolatey.org/install"
                }
                continue
            }
            
            # Standard installation
            if ($hasWinget -and $dep.winget) {
                Write-Host "Installing via winget..."
                winget install --id $dep.winget --silent --accept-package-agreements --accept-source-agreements
            } elseif ($hasChoco -and $dep.choco) {
                Write-Host "Installing via Chocolatey..."
                choco install $dep.choco -y
            } else {
                Write-Error "No package manager available to install $($dep.name)"
                continue
            }
            
            Write-Success "$($dep.name) installed successfully"
            
            # Refresh PATH
            $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path", "User")
            
        } catch {
            Write-Error "Failed to install $($dep.name): $_"
        }
    }
    
    Write-Host ""
    Write-Warning "Dependencies have been installed. You may need to restart your terminal."
    if (-not $Silent) {
        $response = Read-Host "Continue with compilation? (Y/n)"
        if ($response -eq 'n' -or $response -eq 'N') {
            exit 0
        }
    }
}

Write-Success "All required dependencies are available!"
Write-Host ""

# Check for broken MinGW in C:\MinGW
if (Test-Path "C:\MinGW\bin\c++.exe") {
    Write-Warning "Old MinGW installation detected at C:\MinGW"
    Write-Host "  This may interfere with the Chocolatey MinGW installation." -ForegroundColor Yellow
    Write-Host "  If you encounter compiler errors, consider removing C:\MinGW" -ForegroundColor Yellow
}

# ============================================================================
# Step 3: Verify Git repository
# ============================================================================
Write-Info "Step 3/5: Verifying Git repository..."

if (-not (Test-Path (Join-Path $ScriptDir ".git"))) {
    Write-Warning "Not in a Git repository. Checking if NightForge is cloned..."
    
    if (-not (Test-Path (Join-Path $ScriptDir "CMakeLists.txt"))) {
        Write-Error "NightForge source not found. Please clone the repository first:"
        Write-Host "  git clone https://github.com/Lazzzycatwastaken/NightForge.git"
        exit 1
    }
}

Write-Success "Repository verified"
Write-Host ""

# ============================================================================
# Step 4: Compile NightForge
# ============================================================================
if (-not $SkipCompile) {
    Write-Info "Step 4/5: Compiling NightForge..."
    
    # Create build directory
    if (Test-Path $BuildDir) {
        Write-Warning "Build directory exists. Cleaning..."
        Remove-Item $BuildDir -Recurse -Force
    }
    
    New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
    
    # Configure with CMake
    Write-Host "Running CMake configuration..."
    Push-Location $BuildDir
    
    try {
        $cmakeArgs = @("..", "-DCMAKE_BUILD_TYPE=Release")
        $useMinGW = $false
        
        # Try to detect Visual Studio first
        $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        $hasVS = $false
        
        if (Test-Path $vsWhere) {
            $vsPath = & $vsWhere -latest -property installationPath 2>$null
            if ($vsPath) {
                Write-Info "Visual Studio found at: $vsPath"
                $cmakeArgs += "-G", "Visual Studio 17 2022"
                $hasVS = $true
            }
        }
        
        # If no Visual Studio, try MinGW
        if (-not $hasVS) {
            $mingwBin = $null
            
            # First, try using the MinGW we already detected in dependency check
            if ($detectedMingwPath) {
                Write-Info "Using MinGW from PATH: $detectedMingwPath"
                $mingwBin = $detectedMingwPath
            } else {
                # Find MinGW installation - prefer Chocolatey MinGW (more reliable)
                $mingwPaths = @(
                    "C:\ProgramData\chocolatey\lib\mingw\tools\install\mingw64",  # Chocolatey (preferred)
                    "C:\msys64\mingw64",                                          # MSYS2
                    "C:\mingw-w64",                                               # Manual install
                    "C:\mingw64",
                    "$env:LOCALAPPDATA\Programs\mingw-w64"
                    # NOTE: Intentionally NOT including C:\MinGW - often an old/broken installation
                )
                
                foreach ($path in $mingwPaths) {
                    $binPath = Join-Path $path "bin"
                    $gppPath = Join-Path $binPath "g++.exe"
                    if (Test-Path $gppPath) {
                        # Verify this g++ actually works by checking version
                        try {
                            $version = & $gppPath --version 2>&1 | Select-Object -First 1
                            if ($version -match "g\+\+") {
                                $mingwBin = $binPath
                                Write-Info "MinGW found at: $mingwBin"
                                Write-Host "  Version: $version" -ForegroundColor DarkGray
                                break
                            }
                        } catch {
                            Write-Warning "MinGW at $binPath appears broken, skipping..."
                            continue
                        }
                    }
                }
            }
            
            if ($mingwBin) {
                # Put MinGW at the FRONT of PATH to override any broken installations
                $env:Path = "$mingwBin;$env:Path"
                
                # Find mingw32-make.exe
                $makePath = Join-Path $mingwBin "mingw32-make.exe"
                $gppPath = Join-Path $mingwBin "g++.exe"
                $gccPath = Join-Path $mingwBin "gcc.exe"
                
                # Check what we have
                $hasMake = Test-Path $makePath
                $hasGpp = Test-Path $gppPath
                $hasGcc = Test-Path $gccPath
                
                if (-not $hasMake) {
                    Write-Warning "mingw32-make.exe not found at: $makePath"
                }
                if (-not $hasGpp) {
                    Write-Warning "g++.exe not found at: $gppPath"
                }
                if (-not $hasGcc) {
                    Write-Warning "gcc.exe not found at: $gccPath"
                }
                
                if ($hasMake -and $hasGpp -and $hasGcc) {
                    Write-Info "Using MinGW Makefiles generator"
                    Write-Host "  Compiler: $gppPath" -ForegroundColor DarkGray
                    Write-Host "  Make: $makePath" -ForegroundColor DarkGray
                    
                    $cmakeArgs += "-G", "MinGW Makefiles"
                    $cmakeArgs += "-DCMAKE_MAKE_PROGRAM=$makePath"
                    $cmakeArgs += "-DCMAKE_CXX_COMPILER=$gppPath"
                    $cmakeArgs += "-DCMAKE_C_COMPILER=$gccPath"
                    $useMinGW = $true
                } else {
                    Write-Warning "MinGW installation at $mingwBin is incomplete"
                    Write-Host "Try reinstalling: choco install mingw -y --force" -ForegroundColor Yellow
                }
            } else {
                Write-Warning "MinGW installation not found in common locations"
                Write-Host "Checked locations:" -ForegroundColor DarkGray
                Write-Host "  - Detected from PATH: $detectedMingwPath" -ForegroundColor DarkGray
                Write-Host "  - C:\ProgramData\chocolatey\lib\mingw\tools\install\mingw64\bin" -ForegroundColor DarkGray
                Write-Host "  - C:\msys64\mingw64\bin" -ForegroundColor DarkGray
            }
        }
        
        # Fallback to Ninja if available
        if (-not $hasVS -and -not $useMinGW) {
            try {
                $null = Get-Command ninja -ErrorAction Stop
                Write-Info "Using Ninja generator"
                $cmakeArgs += "-G", "Ninja"
            } catch {
                # No suitable build system found
                Write-Error "No suitable build system found!"
                Write-Host ""
                Write-Host "Please install one of the following:" -ForegroundColor Yellow
                Write-Host "  Option 1 (Recommended): Visual Studio Build Tools" -ForegroundColor Cyan
                Write-Host "    winget install Microsoft.VisualStudio.2022.BuildTools" -ForegroundColor Gray
                Write-Host ""
                Write-Host "  Option 2: MinGW-w64 via Chocolatey" -ForegroundColor Cyan
                Write-Host "    choco install mingw -y" -ForegroundColor Gray
                Write-Host ""
                Write-Host "  Option 3: Ninja build system" -ForegroundColor Cyan
                Write-Host "    winget install Ninja-build.Ninja" -ForegroundColor Gray
                Write-Host ""
                Write-Host "After installation, restart PowerShell and run this installer again." -ForegroundColor Yellow
                throw "No C++ compiler or build system found"
            }
        }
        
        Write-Host "CMake arguments: $($cmakeArgs -join ' ')" -ForegroundColor DarkGray
        & cmake @cmakeArgs
        
        if ($LASTEXITCODE -ne 0) {
            Write-Host ""
            Write-Error "CMake configuration failed!"
            Write-Host ""
            Write-Host "Common solutions:" -ForegroundColor Yellow
            Write-Host ""
            Write-Host "  1. If you see 'CMAKE_CXX_COMPILER not set':" -ForegroundColor Yellow
            Write-Host "     Your C++ compiler is not properly installed or detected." -ForegroundColor Gray
            Write-Host "     Install one of these:" -ForegroundColor Gray
            Write-Host "       • Visual Studio Build Tools (Recommended):" -ForegroundColor Cyan
            Write-Host "         winget install Microsoft.VisualStudio.2022.BuildTools" -ForegroundColor DarkGray
            Write-Host "       • MinGW-w64:" -ForegroundColor Cyan
            Write-Host "         choco install mingw -y --force" -ForegroundColor DarkGray
            Write-Host ""
            Write-Host "  2. If you see 'CMAKE_MAKE_PROGRAM is not set':" -ForegroundColor Yellow
            Write-Host "     Your build tool is not found. Try:" -ForegroundColor Gray
            Write-Host "       • Reinstall MinGW:" -ForegroundColor Cyan
            Write-Host "         choco uninstall mingw -y" -ForegroundColor DarkGray
            Write-Host "         choco install mingw -y --force" -ForegroundColor DarkGray
            Write-Host "       • Or use Visual Studio instead (see option 1 above)" -ForegroundColor Cyan
            Write-Host ""
            Write-Host "  3. After installing, RESTART PowerShell and run installer again" -ForegroundColor Yellow
            Write-Host ""
            Write-Host "  4. Verify installation:" -ForegroundColor Yellow
            Write-Host "     where g++" -ForegroundColor DarkGray
            Write-Host "     where mingw32-make" -ForegroundColor DarkGray
            Write-Host ""
            throw "CMake configuration failed"
        }
        
        Write-Success "CMake configuration completed"
        
        # Build
        Write-Host "Building NightForge..."
        & cmake --build . --config Release --parallel
        
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed"
        }
        
        Write-Success "Build completed successfully!"
        
    } catch {
        Write-Error "Compilation failed: $_"
        Pop-Location
        exit 1
    } finally {
        Pop-Location
    }
    
    # Verify executables
    $executables = @("nightforge.exe", "nscompile.exe", "nightscript_runner.exe", "webp_to_ascii.exe")
    $builtExes = @()
    
    foreach ($exe in $executables) {
        $exePath = Join-Path $BuildDir $exe
        if (Test-Path $exePath) {
            $builtExes += $exe
        }
    }
    
    if ($builtExes.Count -eq 0) {
        Write-Error "No executables found after build!"
        exit 1
    }
    
    Write-Host ""
    Write-Success "Built executables:"
    foreach ($exe in $builtExes) {
        Write-Host "  - $exe" -ForegroundColor Green
    }
    
} else {
    Write-Info "Step 4/5: Skipping compilation (--SkipCompile flag set)"
}

Write-Host ""

# ============================================================================
# Step 5: Add to PATH (optional)
# ============================================================================
Write-Info "Step 5/5: PATH configuration..."

if (-not $AddToPath -and -not $Silent) {
    Write-Host ""
    $response = Read-Host "Do you want to add NightForge to your PATH? (y/N)"
    if ($response -eq 'y' -or $response -eq 'Y') {
        $AddToPath = $true
    }
}

if ($AddToPath) {
    if (-not $isAdmin) {
        Write-Warning "Adding to PATH requires administrator privileges."
        Write-Host "You can manually add '$BuildDir' to your PATH."
    } else {
        try {
            $currentPath = [Environment]::GetEnvironmentVariable("Path", "Machine")
            
            if ($currentPath -notlike "*$BuildDir*") {
                Write-Info "Adding $BuildDir to system PATH..."
                [Environment]::SetEnvironmentVariable("Path", "$currentPath;$BuildDir", "Machine")
                
                # Also update current session
                $env:Path += ";$BuildDir"
                
                Write-Success "NightForge added to PATH successfully!"
                Write-Host "You can now run 'nightforge' from anywhere in the terminal."
            } else {
                Write-Success "NightForge is already in PATH"
            }
        } catch {
            Write-Error "Failed to add to PATH: $_"
            Write-Host "You can manually add '$BuildDir' to your PATH."
        }
    }
} else {
    Write-Info "Skipping PATH configuration"
    Write-Host "To use NightForge, run it from: $BuildDir\nightforge.exe"
}

Write-Host ""

# ============================================================================
# Installation Complete
# ============================================================================
Write-Host @"
╔═══════════════════════════════════════════════════════════╗
║                                                           ║
║            Installation Complete!                         ║
║                                                           ║
╚═══════════════════════════════════════════════════════════╝
"@ -ForegroundColor Green

Write-Host ""
Write-Host "Quick Start:" -ForegroundColor Cyan
Write-Host "  1. Test the installation:"
Write-Host "     $BuildDir\nightforge.exe --help"
Write-Host ""
Write-Host "  2. Run a NightScript file:"
Write-Host "     $BuildDir\nightforge.exe examples\elseif_demo.ns"
Write-Host ""
Write-Host "  3. Compile a script:"
Write-Host "     $BuildDir\nscompile.exe script.ns"
Write-Host ""

if ($AddToPath) {
    Write-Host "NightForge has been added to your PATH. Restart your terminal to use it globally." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "For more information, see: README.md" -ForegroundColor DarkGray
Write-Host ""
