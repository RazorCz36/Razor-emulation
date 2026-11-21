# ==============================================================================
# RazorEmulations - Private LAN / NAS Store Host (PowerShell 5.1+ / 7+)
# Strictly 7-bit ASCII for universal Windows locale compatibility.
#
# Usage:
#   .\store\host_private_store.ps1 -GamesPath "D:\MyPS5Backups" -Port 8765
#
# Then in RazorEmulations Launcher -> click "RAZOR STORE" -> "+ Custom Store URL"
# and enter:
#   http://localhost:8765/catalog.json   (or http://<Your-LAN-IP>:8765/catalog.json)
# ==============================================================================

param(
    [string]$GamesPath = ".\Packages",
    [int]$Port = 8765,
    [string]$HostAddress = "localhost"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $GamesPath)) {
    New-Item -ItemType Directory -Path $GamesPath -Force | Out-Null
}
$ResolvedRoot = (Resolve-Path $GamesPath).Path

Write-Host ""
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "  RazorEmulations Private Store Generator & HTTP Server" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "Scanning packages in: $ResolvedRoot"

$PackageFiles = Get-ChildItem -Path $ResolvedRoot -File | Where-Object {
    $_.Extension -in @(".zar", ".zip", ".tgz", ".elf", ".bin") -or $_.Name.EndsWith(".tar.gz")
}

$Items = @()
$Index = 1
foreach ($File in $PackageFiles) {
    $Sha = (Get-FileHash -Path $File.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $BaseName = [System.IO.Path]::GetFileNameWithoutExtension($File.Name)
    $TitleId = if ($BaseName -match "^PPSA\d{5}$") { $BaseName.ToUpperInvariant() } else { ("PPSA8{0:D4}" -f $Index) }
    $Ext = $File.Extension.TrimStart('.').ToLowerInvariant()
    if ($File.Name.ToLowerInvariant().EndsWith(".tar.gz")) { $Ext = "tar.gz" }

    $Items += [ordered]@{
        title_id     = $TitleId
        name         = $BaseName
        subtitle     = "Personal PS5 Package ($($File.Name))"
        category     = "Personal Backup"
        version      = "01.00"
        author       = "Local Vault"
        license      = "Personal / Self-Dumped Backup"
        resolutions  = "1080p FHD / 1440p WQHD / 4K UHD"
        controls     = "DualSense Controller or Mapped PC Keyboard"
        description  = "Served from your private Razor Store folder ($($File.Name)). Click Download & Auto-Install to install directly into RazorEmulations."
        format       = $Ext
        size_bytes   = [int64]$File.Length
        sha256       = $Sha
        download_url = "http://${HostAddress}:${Port}/$([uri]::EscapeDataString($File.Name))"
    }
    $Index++
}

$Catalog = [ordered]@{
    store_name    = "Razor Private LAN Vault"
    store_version = 1
    updated_at    = (Get-Date -Format "yyyy-MM-dd")
    items         = $Items
}

$CatalogPath = Join-Path $ResolvedRoot "catalog.json"
$CatalogJson = $Catalog | ConvertTo-Json -Depth 6
[System.IO.File]::WriteAllText($CatalogPath, $CatalogJson, [System.Text.Encoding]::UTF8)

Write-Host ("Generated catalog.json with {0} package(s)." -f $Items.Count) -ForegroundColor Green
Write-Host ("Catalog URL for Razor Store: http://{0}:{1}/catalog.json" -f $HostAddress, $Port) -ForegroundColor Yellow
Write-Host "Press Ctrl+C to stop serving."
Write-Host ""

$Listener = New-Object System.Net.HttpListener
$Listener.Prefixes.Add("http://${HostAddress}:${Port}/")
$Listener.Start()

try {
    while ($Listener.IsListening) {
        $Context = $Listener.GetContext()
        $RelPath = [uri]::UnescapeDataString($Context.Request.Url.AbsolutePath.TrimStart('/'))
        if ([string]::IsNullOrWhiteSpace($RelPath)) {
            $RelPath = "catalog.json"
        }
        $SafeName = [System.IO.Path]::GetFileName($RelPath)
        $FullTarget = Join-Path $ResolvedRoot $SafeName

        if (Test-Path $FullTarget -PathType Leaf) {
            $Context.Response.StatusCode = 200
            if ($SafeName.EndsWith(".json")) {
                $Context.Response.ContentType = "application/json; charset=utf-8"
            } else {
                $Context.Response.ContentType = "application/octet-stream"
            }
            $Stream = [System.IO.File]::OpenRead($FullTarget)
            $Context.Response.ContentLength64 = $Stream.Length
            try {
                $Stream.CopyTo($Context.Response.OutputStream)
            } finally {
                $Stream.Dispose()
                $Context.Response.OutputStream.Close()
            }
        } else {
            $Context.Response.StatusCode = 404
            $Context.Response.OutputStream.Close()
        }
    }
} finally {
    $Listener.Stop()
}
