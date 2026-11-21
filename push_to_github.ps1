<#
.SYNOPSIS
    Creates a new GitHub repository named "RazorEmulations" on your GitHub account
    and pushes the complete codebase (triggering automated Windows 11 x64 Release builds).

.EXAMPLE
    .\push_to_github.ps1
    .\push_to_github.ps1 -RepoName "RazorEmulations" -Visibility "public"
#>
param(
    [string]$RepoName = "RazorEmulations",
    [ValidateSet("public", "private")]
    [string]$Visibility = "public"
)

$ErrorActionPreference = "Stop"
Set-Location -Path $PSScriptRoot

Write-Host "=== RazorEmulations — GitHub Repository Setup ===" -ForegroundColor Yellow

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "Git is not installed or not on PATH. Install Git from https://git-scm.com/download/win"
}

if (Get-Command gh -ErrorAction SilentlyContinue) {
    Write-Host "[1/3] GitHub CLI detected. Checking authentication..." -ForegroundColor Cyan
    gh auth status
    if ($LASTEXITCODE -ne 0) {
        gh auth login
    }

    if (git remote | Select-String -Pattern "^origin$") {
        git remote remove origin
    }

    Write-Host "[2/3] Creating repository '$RepoName' ($Visibility) and pushing 'main'..." -ForegroundColor Cyan
    gh repo create $RepoName --$Visibility --source=. --remote=origin --push
    Write-Host "[3/3] Success! GitHub Actions is now building your Windows 11 x64 release under the Actions tab." -ForegroundColor Green
} else {
    Write-Host "GitHub CLI ('gh') not found. Using standard Git HTTPS push:" -ForegroundColor Cyan
    $GitHubUser = Read-Host "Enter your GitHub username"
    Write-Host "1. Open https://github.com/new and create an EMPTY repository named '$RepoName' (do NOT add README/license)." -ForegroundColor Yellow
    Read-Host "2. Press Enter once you have created https://github.com/$GitHubUser/$RepoName"

    $RemoteUrl = "https://github.com/$GitHubUser/$RepoName.git"
    if (git remote | Select-String -Pattern "^origin$") {
        git remote set-url origin $RemoteUrl
    } else {
        git remote add origin $RemoteUrl
    }

    git branch -M main
    git push -u origin main
    Write-Host "Pushed successfully to $RemoteUrl!" -ForegroundColor Green
}
