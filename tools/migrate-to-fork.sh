#!/usr/bin/env bash
# Build the PS5MC branches inside a clone of the GitHub fork of xbmc/xbmc.
#
#   ps5mc-piers = upstream Piers (Kodi 22) + the ps5 platform series
#   ps5mc-omega = upstream Omega (Kodi 21) + the same series (8 Omega forms)
#
# The series = the 19 patches as commits, the platform files as 4 grouped
# commits, the build infra under ps5/. Verified offline end to end (24 commits
# per branch). Idempotent: re-running recreates both branches from scratch.
#
# Usage, from inside the fork clone:
#   git clone https://github.com/VivaLaVent/PS5MC.git && cd PS5MC
#   bash /mnt/c/kodi-ps5/tools/migrate-to-fork.sh /mnt/c/kodi-ps5
set -euo pipefail
SRC="${1:?usage: migrate-to-fork.sh <path to kodi-ps5 checkout>}"
SRC="$(cd "$SRC" && pwd)"
[ -d "$SRC/overlay" ] && [ -d "$SRC/patches/kodi" ] || { echo "!! $SRC is not the kodi-ps5 checkout (needs overlay/ and patches/kodi)"; exit 1; }
git rev-parse --is-inside-work-tree >/dev/null 2>&1 || { echo "!! run from inside the PS5MC fork clone"; exit 1; }
[ -f xbmc/Application.cpp ] || [ -f xbmc/application/Application.cpp ] || { echo "!! this is not a Kodi tree"; exit 1; }
[ -z "$(git status --porcelain)" ] || { echo "!! fork working tree not clean"; exit 1; }

echo "==> fetching upstream Piers and Omega (the fork's 'origin' is the fork; upstream = xbmc/xbmc)"
git remote get-url upstream >/dev/null 2>&1 || git remote add upstream https://github.com/xbmc/xbmc.git
git fetch --filter=blob:none upstream Piers Omega

P="$SRC/patches/kodi"; PO="$SRC/patches/omega"

apply_series() { # $1 = branch, $2 = upstream ref, $3 = omega|piers
  echo "==> building $1 on $2"
  git checkout -q -B "$1" "$2"
  local applied=0
  for f in "$P"/0*.patch; do
    local n; n="$(basename "$f" .patch)"
    local pf="$f"
    [ "$3" = omega ] && [ -f "$PO/$n.patch" ] && pf="$PO/$n.patch"
    if git apply --index "$pf" 2>/dev/null \
       || { git checkout -q -- . && git apply --index --3way "$pf" 2>/dev/null; } \
       || { git checkout -q -- . && patch -p1 --forward -F3 --no-backup-if-mismatch -r - --silent < "$pf" >/dev/null 2>&1 && git add -A; }; then
      git commit -q -m "ps5: $n" && applied=$((applied+1))
    else
      git checkout -q -- .; git clean -qfd
      echo "!! $n does not apply on $1 - port it (docs/FORK-PLAN.md) and re-run"; exit 1
    fi
  done
  echo "   $applied patches applied as commits"
  for grp in cmake xbmc/windowing/ps5 xbmc/platform/ps5 system; do
    (cd "$SRC/overlay" && find "$grp" -type f 2>/dev/null) | while read -r f; do mkdir -p "$(dirname "$f")"; cp "$SRC/overlay/$f" "$f"; done
    git add -A "$grp" 2>/dev/null; git diff --cached --quiet || git commit -q -m "ps5: platform files - $grp"
  done
  mkdir -p ps5
  for d in scripts shims pacbrew toolchain tools title docs addons; do [ -d "$SRC/$d" ] && cp -r "$SRC/$d" ps5/; done
  # the fork's ps5/README.md is the full project README (the old repo's README is now a pointer)
  cp "$SRC/docs/PS5MC-README.md" ps5/README.md
  cp "$SRC/LICENSE" ps5/LICENSE 2>/dev/null || true
  mkdir -p ps5/patches && cp -r "$P" ps5/patches/kodi && cp -r "$PO" ps5/patches/omega 2>/dev/null || true
  echo "# In the fork these are commits in the ps5 series; kept for reference." > ps5/patches/README.md
  rm -f ps5/tools/migrate-to-fork.sh   # one-shot; does not belong in the fork
  git add -A ps5 && git commit -q -m "ps5: build infrastructure under ps5/"
  echo "   $1: $(git log --oneline "$2".."$1" | wc -l) commits on top of $2"
}

apply_series ps5mc-piers upstream/Piers piers
apply_series ps5mc-omega upstream/Omega omega
git checkout -q ps5mc-piers
echo
echo "==> done. Push both:   git push -u origin ps5mc-piers ps5mc-omega"
echo "    then make ps5mc-piers the default branch on GitHub (Settings > General)."
echo "    Build 22:  KODI_SRC=\$PWD BUILD=~/ps5mc-build-22 bash ps5/scripts/20-configure-kodi.sh"
echo "    Build 21:  git checkout ps5mc-omega && KODI_SRC=\$PWD BUILD=~/ps5mc-build-21 bash ps5/scripts/20-configure-kodi.sh"
