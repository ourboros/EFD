$content = Get-Content "package_windows.ps1" -Encoding UTF8 -Raw
$content = $content -replace 'EFD.exe\\" /f','EFD.exe\\" --background /f'
Set-Content -Path "package_windows_utf8.ps1" -Value $content -Encoding UTF8
