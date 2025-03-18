# Get the VS Code extensions directory
$VSCodeExtDir = "$env:USERPROFILE\.vscode\extensions"

# Find the latest installed clang-format in the VS Code extensions folder
$ClangFormatPath = Get-ChildItem -Path $VSCodeExtDir -Recurse -Filter "clang-format.exe" | 
                   Sort-Object FullName -Descending | 
                   Select-Object -First 1 -ExpandProperty FullName

# Check if clang-format was found
if (-not $ClangFormatPath) {
    Write-Host "clang-format not found in VS Code extensions. Please install the C/C++ extension." -ForegroundColor Red
    exit 1
}

Write-Host "Using clang-format: $ClangFormatPath" -ForegroundColor Cyan

# Process each file and check if changes were made
$Files = Get-ChildItem -Path src/vario, src/variants -Recurse -Include *.cpp, *.h

foreach ($File in $Files) {
    $OriginalHash = Get-FileHash -Path $File.FullName -Algorithm SHA256
    & $ClangFormatPath -i $File.FullName
    $NewHash = Get-FileHash -Path $File.FullName -Algorithm SHA256

    if ($OriginalHash.Hash -ne $NewHash.Hash) {
        Write-Host "Formatted: $($File.FullName)" -ForegroundColor Green
    }
}

Write-Host "Formatting complete."
