# 把 debug_frames/ (含子資料夾) 裡的 JPG / PNG 轉成 efd_diag_replay 可讀的 PPM
# iOS 診斷模式匯出的照片經 LINE 傳回後是 JPG，轉檔後即可重播：
#   powershell -File tests/convert_to_ppm.ps1 [資料夾]
#   build/verify/tests/Debug/efd_diag_replay.exe debug_frames
param([string]$Folder = (Join-Path $PSScriptRoot "..\debug_frames"))

Add-Type -AssemblyName System.Drawing

$images = Get-ChildItem -Path $Folder -Recurse -File -Include *.jpg, *.jpeg, *.png
foreach ($file in $images) {
    $target = [System.IO.Path]::ChangeExtension($file.FullName, ".ppm")
    if (Test-Path $target) { continue }

    $img = [System.Drawing.Image]::FromFile($file.FullName)
    $w = $img.Width; $h = $img.Height
    $bmp = New-Object System.Drawing.Bitmap $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.DrawImage($img, 0, 0, $w, $h)
    $g.Dispose(); $img.Dispose()

    $data = $bmp.LockBits((New-Object System.Drawing.Rectangle 0, 0, $w, $h), 'ReadOnly', $bmp.PixelFormat)
    $stride = $data.Stride
    $buf = New-Object byte[] ($stride * $h)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $buf, 0, $buf.Length)
    $bmp.UnlockBits($data); $bmp.Dispose()

    # GDI+ 為 BGR 排列，PPM 需要 RGB
    $header = [System.Text.Encoding]::ASCII.GetBytes("P6`n$w $h`n255`n")
    $out = New-Object byte[] ($header.Length + $w * $h * 3)
    [Array]::Copy($header, $out, $header.Length)
    $o = $header.Length
    for ($y = 0; $y -lt $h; $y++) {
        $row = $y * $stride
        for ($x = 0; $x -lt $w; $x++) {
            $i = $row + $x * 3
            $out[$o] = $buf[$i + 2]; $out[$o + 1] = $buf[$i + 1]; $out[$o + 2] = $buf[$i]
            $o += 3
        }
    }
    [System.IO.File]::WriteAllBytes($target, $out)
    Write-Output "converted: $($file.Name) ($w x $h)"
}
