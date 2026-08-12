function Show-MessageBox {
    param ([string]$message)
    Add-Type -AssemblyName PresentationFramework
    [System.Windows.MessageBox]::Show($message, "Bootstrap Error", 'OK', 'Warning')
}

# Resolve repository-relative files from the script directory.  PowerShell's
# current directory belongs to the caller and is not changed by -File.
Set-Location -LiteralPath $PSScriptRoot

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Show-MessageBox "Git is needed to checkout vcpkg, but it's not installed or not available in PATH."
    exit 1
}

git submodule update --init --recursive
if ($LASTEXITCODE -ne 0) {
    Show-MessageBox "Unable to initialize the source submodules. Check the Git output above."
    exit 1
}

if (-not (Test-Path "vcpkg/.git")) {
    if (-not (Test-Path "version.json")) {
        Show-MessageBox "Unable to checkout correct version of vcpkg without 'version.json'."
        exit 1
    }

    try {
        $versionData = Get-Content -Raw -Path "version.json" | ConvertFrom-Json
    } catch {
        Show-MessageBox "version.json is not valid JSON."
        exit 1
    }

    $vcpkgVersion = $versionData.vcpkg
    if ([string]::IsNullOrWhiteSpace($vcpkgVersion)) {
        Show-MessageBox "version.json does not contain a valid 'vcpkg' version."
        exit 1
    }

    if (Test-Path "vcpkg") {
        Remove-Item -Recurse -Force "vcpkg"
    }

    git clone --branch $vcpkgVersion --depth 1 https://github.com/microsoft/vcpkg.git "vcpkg"
    $cloneExitCode = $LASTEXITCODE

    if ($cloneExitCode -ne 0) {
        Show-MessageBox "Checkout of vcpkg failed."
        exit 1
    }
}

& "vcpkg/bootstrap-vcpkg.bat" -disableMetrics
if ($LASTEXITCODE -ne 0) {
    Show-MessageBox "vcpkg bootstrap failed. Check the console output above."
    exit 1
}
