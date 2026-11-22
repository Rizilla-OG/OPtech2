#!/bin/sh
# Runs CodeQL like CI (.github/workflows/codeql.yml: C/C++, the default
# code-scanning suite): traces a fresh build in build/codeql, analyses it,
# drops bundled libraries and prints one line per result.
# Needs the CodeQL CLI with its C/C++ queries; Vulkan is built when
# /usr/include/vulkan/vk_platform.h exists. Usage: tools/ci/run_codeql.sh
set -e
cd "$(dirname "$0")/../.."
engine="$(pwd)"
build="$engine/build/codeql/build"
database="$engine/build/codeql/database"
sarif="$engine/build/codeql/results.sarif"
rm -rf "$build" "$database"
vulkan=OFF
[ -f /usr/include/vulkan/vk_platform.h ] && vulkan=ON
cmake -S . -B "$build" -DSDL3_SUPPORT=OFF -DVK_RENDERER=$vulkan >/dev/null
codeql database create "$database" --language=cpp --source-root="$engine" \
  --command="cmake --build $build -j$(nproc)" >/dev/null
# the default RAM limit is too small for this code base
codeql database analyze "$database" codeql/cpp-queries:codeql-suites/cpp-code-scanning.qls \
  --format=sarif-latest --output="$sarif" --threads=8 --ram=32000 >/dev/null
python3 tools/ci/filter_sarif.py "$sarif"
python3 - "$sarif" <<'PY'
import json, sys
results = [result for run in json.load(open(sys.argv[1]))["runs"] for result in run["results"]]
print(len(results), "results")
for result in results:
    location = result["locations"][0]["physicalLocation"]
    print(f'{result["ruleId"]} {location["artifactLocation"]["uri"]}:{location["region"]["startLine"]}')
PY
