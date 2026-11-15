# Pixel size of a 109u x 193u dialog control with a "Segoe UI" 9pt dialog font, per DPI.
Add-Type @'
using System; using System.Runtime.InteropServices;
public class G {
  [DllImport("gdi32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr CreateFontW(int h,int w,int e,int o,int wt,uint i,uint u,uint s,uint cs,uint op,uint cp,uint q,uint pf,string face);
  [DllImport("gdi32.dll")] public static extern IntPtr CreateCompatibleDC(IntPtr dc);
  [DllImport("gdi32.dll")] public static extern IntPtr SelectObject(IntPtr dc, IntPtr o);
  [DllImport("gdi32.dll", CharSet=CharSet.Unicode)] public static extern bool GetTextExtentPoint32W(IntPtr dc, string s, int n, out SZ sz);
  [DllImport("gdi32.dll", CharSet=CharSet.Unicode)] public static extern bool GetTextMetricsW(IntPtr dc, out TM tm);
  public struct SZ { public int cx, cy; }
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
  public struct TM { public int tmHeight,tmAscent,tmDescent,tmInternalLeading,tmExternalLeading,tmAveCharWidth,tmMaxCharWidth,tmWeight,tmOverhang,tmDigitizedAspectX,tmDigitizedAspectY; public char a,b,c,d; public byte e,f,g,h,i; }
}
'@
$dc = [G]::CreateCompatibleDC([IntPtr]::Zero)
$s = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz'
foreach ($dpi in 96,120,144,168,192,216,240,288) {
  $h = -[int][math]::Floor((9 * $dpi + 36) / 72)   # -MulDiv(9, dpi, 72)
  $f = [G]::CreateFontW($h,0,0,0,400,0,0,0,1,0,0,0,0,'Segoe UI')
  [G]::SelectObject($dc, $f) | Out-Null
  $sz = New-Object G+SZ; [G]::GetTextExtentPoint32W($dc, $s, 52, [ref]$sz) | Out-Null
  $tm = New-Object G+TM; [G]::GetTextMetricsW($dc, [ref]$tm) | Out-Null
  $cx = [int][math]::Floor(([math]::Floor($sz.cx / 26) + 1) / 2)
  $cy = $tm.tmHeight
  $w = [int][math]::Floor((109 * $cx + 2) / 4)
  $hh = [int][math]::Floor((193 * $cy + 4) / 8)
  "{0} {1} {2}" -f $dpi, $w, $hh
}
