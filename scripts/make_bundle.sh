#!/bin/sh
# Build a self-contained ESPHome bundle from example.yaml.
# Usage: scripts/make_bundle.sh <output.tar.gz>
# The component is switched to a local source so the bundle builds without
# GitHub, and secrets.yaml holds only the placeholders from
# secrets.example.yaml.
set -eu

out=$(realpath -m "$1")
repo=$(cd "$(dirname "$0")/.." && pwd)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

cp "$repo/example.yaml" "$stage/"
cp -r "$repo/components" "$stage/"
cp "$repo/secrets.example.yaml" "$stage/secrets.yaml"

python3 - "$stage/example.yaml" <<'PY'
import sys
path = sys.argv[1]
s = open(path).read()
old = """  - source: github://doaelectronics-coder/esphome-ooycyoo-mppt@main
    components: [ooycyoo_mppt]
  # To use a local checkout instead:
  # - source:
  #     type: local
  #     path: components
"""
new = """  - source:
      type: local
      path: components
    components: [ooycyoo_mppt]
"""
if old not in s:
    sys.exit("example.yaml: external_components block not found")
open(path, "w").write(s.replace(old, new))
PY

cd "$stage"
esphome config example.yaml > /dev/null
esphome bundle -o "$out" example.yaml
