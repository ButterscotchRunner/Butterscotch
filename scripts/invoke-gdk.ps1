param(
  [Parameter(Mandatory = $true, Position = 0)]
  [string]$Command
)

$gdkPrompt = @(
  "${env:ProgramFiles(x86)}\Microsoft GDK\Command Prompts\Desktop VS 2026 Gaming Command Prompt.lnk",
  "${env:ProgramFiles}\Microsoft GDK\Command Prompts\Desktop VS 2026 Gaming Command Prompt.lnk"
) |
  Where-Object { $_ -and (Test-Path $_) } |
  Select-Object -First 1

if (-not $gdkPrompt) {
  throw 'Microsoft GDK Desktop VS 2026 Gaming Command Prompt.lnk was not found.'
}

$shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut($gdkPrompt)
$target = $shortcut.TargetPath
$args = $shortcut.Arguments

if (-not $target) {
  throw 'The GDK command prompt shortcut target could not be resolved.'
}

$launcher = if ($args) { '"{0}" {1}' -f $target, $args } else { '"{0}"' -f $target }
$cmdline = $launcher + ' && ' + $Command

cmd.exe /d /s /c $cmdline
