# README recording helper: UI Automation lookups for scripts/readme_record.py.
# Reads one JSON request per line on stdin and answers one JSON line on stdout:
#   {"pid": 123, "name": "regex", "type": "Button|TabItem|...", "window": "Sol|Sol — Panel"}
# -> {"found": true, "rect": [left, top, right, bottom], "name": "..."} (screen pixels)
# Only reads the accessibility tree of the given process; never acts on it.
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$A = [System.Windows.Automation.AutomationElement]
[Console]::Out.WriteLine('{"ready":true}')
while ($null -ne ($line = [Console]::In.ReadLine())) {
  try {
    $q = $line | ConvertFrom-Json
    $windows = $A::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children,
      (New-Object System.Windows.Automation.PropertyCondition($A::ProcessIdProperty, [int]$q.pid)))
    $hit = $null
    foreach ($w in $windows) {
      if ($q.window -and $w.Current.Name -ne $q.window) { continue }
      $all = $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
      foreach ($e in $all) {
        $c = $e.Current
        if (!$c.Name -or $c.Name -notmatch $q.name) { continue }
        if ($q.type -and $c.ControlType.ProgrammaticName -notmatch $q.type) { continue }
        $r = $c.BoundingRectangle
        # Skip hidden copies: the floating sidebar waits off-screen (x around -32000) with the same names.
        if ($r.IsEmpty -or $r.Width -lt 1 -or $c.IsOffscreen -or $r.Left -lt -100 -or $r.Top -lt -100) { continue }
        if ($q.within -and ($r.Left + $r.Width / 2 -lt $q.within[0] -or $r.Left + $r.Width / 2 -gt $q.within[2] -or
                            $r.Top + $r.Height / 2 -lt $q.within[1] -or $r.Top + $r.Height / 2 -gt $q.within[3])) { continue }
        $hit = @{ found = $true; name = $c.Name; type = $c.ControlType.ProgrammaticName; rect = @([int]$r.Left, [int]$r.Top, [int]$r.Right, [int]$r.Bottom) }
        break
      }
      if ($hit) { break }
    }
    if (!$hit) { $hit = @{ found = $false } }
    [Console]::Out.WriteLine(($hit | ConvertTo-Json -Compress))
  } catch {
    [Console]::Out.WriteLine((@{ found = $false; error = "$_" } | ConvertTo-Json -Compress))
  }
}
