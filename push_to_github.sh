#!/usr/bin/env bash
set -euo pipefail

REPO_NAME="${1:-RazorEmulations}"
VISIBILITY="${2:-public}"

cd "$(dirname "$0")"
echo "=== RazorEmulations — GitHub Repository Setup ==="

if command -v gh >/dev/null 2>&1; then
    gh auth status || gh auth login
    git remote remove origin 2>/dev/null || true
    gh repo create "$REPO_NAME" "--$VISIBILITY" --source=. --remote=origin --push
    echo "Success! Pushed to GitHub and triggered Windows 11 x64 GitHub Actions build."
else
    read -rp "Enter your GitHub username: " GH_USER
    echo "1. Create an EMPTY repository named '$REPO_NAME' at https://github.com/new"
    read -rp "2. Press Enter once https://github.com/$GH_USER/$REPO_NAME is created..."
    git remote remove origin 2>/dev/null || true
    git remote add origin "https://github.com/$GH_USER/$REPO_NAME.git"
    git branch -M main
    git push -u origin main
fi
