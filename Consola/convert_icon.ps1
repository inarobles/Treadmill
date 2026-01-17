Add-Type -AssemblyName System.Drawing
$imgPath = 'c:\Esp\Treadmill\Consola\icon_resized.png'
$outPath = 'c:\Esp\Treadmill\Consola\main\icon_main.c'

if (!(Test-Path $imgPath)) {
    Write-Error "Resized image not found"
    exit
}

$img = [System.Drawing.Image]::FromFile($imgPath)
$bmp = New-Object System.Drawing.Bitmap($img)
$width = $bmp.Width
$height = $bmp.Height

$sw = New-Object System.IO.StreamWriter($outPath, $false, [System.Text.Encoding]::UTF8)
$sw.WriteLine('#include "lvgl.h"')
$sw.WriteLine('')
$sw.WriteLine('#ifndef LV_ATTRIBUTE_MEM_ALIGN')
$sw.WriteLine('#define LV_ATTRIBUTE_MEM_ALIGN')
$sw.WriteLine('#endif')
$sw.WriteLine('')
$sw.WriteLine('const LV_ATTRIBUTE_MEM_ALIGN uint8_t icon_main_map[] = {')

for ($y = 0; $y -lt $height; $y++) {
    $row = "    "
    for ($x = 0; $x -lt $width; $x++) {
        $color = $bmp.GetPixel($x, $y)
        
        # Convert to RGB565
        $r = [int]($color.R * 31 / 255)
        $g = [int]($color.G * 63 / 255)
        $b = [int]($color.B * 31 / 255)
        $rgb565 = ($r -shl 11) -bor ($g -shl 5) -bor $b
        
        $a = $color.A
        
        # Output as bytes: Color_Low, Color_High, Alpha
        $low = $rgb565 -band 0xFF
        $high = ($rgb565 -shr 8) -band 0xFF
        $row += "0x{0:x2}, 0x{1:x2}, " -f $low, $high
        
        if ($x -gt 0 -and $x % 12 -eq 0) {
            $sw.Write($row)
            $sw.WriteLine("")
            $row = "    "
        }
    }
    if ($row.Trim().Length -gt 0) {
        $sw.WriteLine($row)
    }
}

$sw.WriteLine('};')
$sw.WriteLine('')
$sw.WriteLine('const lv_img_dsc_t icon_main = {')
$sw.WriteLine('  .header.always_zero = 0,')
$sw.WriteLine('  .header.reserved = 0,')
$sw.WriteLine('  .header.w = ' + $width + ',')
$sw.WriteLine('  .header.h = ' + $height + ',')
$sw.WriteLine('  .data_size = ' + ($width * $height * 2) + ',')
$sw.WriteLine('  .header.cf = LV_IMG_CF_TRUE_COLOR,')
$sw.WriteLine('  .data = icon_main_map,')
$sw.WriteLine('};')

$sw.Close()
$bmp.Dispose()
$img.Dispose()
Write-Host "C array generated: $outPath ($width x $height)"
