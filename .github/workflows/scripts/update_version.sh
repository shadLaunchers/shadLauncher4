#!/usr/bin/env bash
set -e

# Always operate from the repository root
cd "$GITHUB_WORKSPACE"

# Ensure we have tags
git fetch --tags --force

# Get latest tag or default
latest_tag=$(git describe --tags --abbrev=0 2>/dev/null || echo "0.0.0")

# Count commits since the latest tag
if git rev-parse "$latest_tag" >/dev/null 2>&1; then
  commit_count=$(git rev-list "${latest_tag}..HEAD" --count)
else
  commit_count=$(git rev-list HEAD --count)
fi

# Short hash and date
short_hash=$(git rev-parse --short HEAD)
date_str=$(date +%Y%m%d)

# Combine into version string (e.g., 1.2.3-45-20251022-ab12cd3)
version="${latest_tag} build ${commit_count} ${date_str}-${short_hash}"

# Save to version.txt at repo root
echo "$version" > "$GITHUB_WORKSPACE/version.txt"

# Output for debugging and GitHub Actions
echo "version=$version" >> "$GITHUB_OUTPUT"
echo "Computed version: $version"
