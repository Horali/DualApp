#!/usr/bin/env bash
# Re-create the exact NewBlackbox checkout audited in Phase 0.
set -euo pipefail
cd "$(dirname "$0")"
COMMIT=89b59836c66f173756a4ae258cf379a957649820
[ -d NewBlackbox ] || git clone https://github.com/ALEX5402/NewBlackbox.git
git -C NewBlackbox checkout "$COMMIT"

# Phase 0 patches (compatibility only; no spoofing changes)
for p in patches/*.patch; do git -C NewBlackbox apply --check "../$p" 2>/dev/null && git -C NewBlackbox apply "../$p"; done
