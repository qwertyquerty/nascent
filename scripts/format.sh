#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"

mapfile -t candidates < <(git ls-files --cached --others --exclude-standard -- 'src/*.h' 'src/*.cpp' 'tests/*.h' 'tests/*.cpp' 'server/native/*.cpp' | grep -v '^src/libs/' | sort -u)
files=()

for file in "${candidates[@]}"; do
    if [ -f "$file" ]; then
        files+=("$file")
    fi
done

if [ "${1:-}" = "--check" ]; then
    if "$CLANG_FORMAT" --dry-run --Werror --style=file "${files[@]}"; then
        echo "All ${#files[@]} files are formatted"
    else
        echo "Formatting issues found, run scripts/format.sh to fix them"
        exit 1
    fi
else
    "$CLANG_FORMAT" -i --style=file "${files[@]}"
    echo "Formatted ${#files[@]} files"
fi
