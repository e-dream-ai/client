param([string]$Src, [string]$OutDir, [string[]]$Sizes)
# Renders the installer Welcome/Finish side panel at each exact WxH, cropped from the
# 1320x816 DMG background so the moon, stars and clouds are kept at every aspect ratio.
Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Bitmap]::FromFile($Src)
$ch = $img.Height
foreach ($s in $Sizes) {
  $W, $H = $s -split 'x' | ForEach-Object { [int]$_ }
  $cw = [int][math]::Round($ch * $W / $H)
  $crop = New-Object System.Drawing.Bitmap $cw, $ch
  $g = [System.Drawing.Graphics]::FromImage($crop)
  $g.DrawImage($img, (New-Object System.Drawing.Rectangle 0,0,$cw,$ch), (New-Object System.Drawing.Rectangle 0,0,$cw,$ch), [System.Drawing.GraphicsUnit]::Pixel)
  $g.Dispose()
  # Paint over the "Drag ... to install" text and the icon-label pill with the row's sky color.
  foreach ($r in @(@(95,160,320), @(515,585,200))) {
    for ($y = $r[0]; $y -lt $r[1]; $y++) {
      $c = $img.GetPixel(20, $y)
      for ($x = $r[2]; $x -lt $cw; $x++) { $crop.SetPixel($x, $y, $c) }
    }
  }
  $dst = New-Object System.Drawing.Bitmap $W, $H, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
  $g = [System.Drawing.Graphics]::FromImage($dst)
  $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
  $attr = New-Object System.Drawing.Imaging.ImageAttributes
  $attr.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)  # no dark fringe at the edges
  $g.DrawImage($crop, (New-Object System.Drawing.Rectangle 0,0,$W,$H), 0, 0, $cw, $ch, [System.Drawing.GraphicsUnit]::Pixel, $attr)
  $g.Dispose()
  $dst.Save((Join-Path $OutDir "welcome-${W}x${H}.bmp"), [System.Drawing.Imaging.ImageFormat]::Bmp)
  $crop.Dispose(); $dst.Dispose()
}
