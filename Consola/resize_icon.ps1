Add-Type -AssemblyName System.Drawing
$inPath = 'c:\Esp\Treadmill\Consola\icono pagina principal.png'
$outPath = 'c:\Esp\Treadmill\Consola\icon_resized.png'
$newWidth = 320
$newHeight = 297

$img = [System.Drawing.Image]::FromFile($inPath)
$bmp = New-Object System.Drawing.Bitmap($newWidth, $newHeight)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.DrawImage($img, 0, 0, $newWidth, $newHeight)
$bmp.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose()
$bmp.Dispose()
$img.Dispose()
Write-Host "Resized to $newWidth x $newHeight"
