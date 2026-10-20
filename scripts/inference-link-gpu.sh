#!/usr/bin/env bash
# Follow PSllama's LLD18 weak dispatch treatment. An absent optional Mesa
# entrypoint is NULL by ELF weak-symbol semantics, never a fabricated import.
# Reference: mansanitizer/psllama d079717184d8153793789f13ed157c1c70390ba0,
# app/build-gguf-vk-title.sh; GPL-3.0-or-later.
set -euo pipefail
sdk=${1:?SDK required}
shift
output=
previous=
for arg in "$@"; do
  if [[ $previous == -o ]]; then output=$arg; fi
  previous=$arg
done
[[ -n $output ]] || { echo 'GPU link output missing' >&2; exit 2; }
"$sdk/bin/prospero-lld" "$@"
weak="${output%.elf}.weak-unresolved.txt"
response="${output%.elf}.weak-null.rsp"
llvm-readelf-18 --dyn-syms "$output" |
  awk '$7 == "UND" && $5 == "WEAK" {print $8}' | sort -u > "$weak"
allowed='^((radv|vk_cmd|vk_common|wsi|ctx_roll|rmv|rra|utrace|sqtt|annotate|threaded)_|_ZTH)'
if grep -vE "$allowed" "$weak" | grep -q .; then
  echo 'Unexpected undefined weak references; refusing to hide them:' >&2
  grep -vE "$allowed" "$weak" >&2
  exit 1
fi
sed 's/.*/--defsym=&=0/' "$weak" > "$response"
"$sdk/bin/prospero-lld" "$@" "@$response"
echo "Bound $(wc -l < "$weak") absent optional Mesa weak entries to NULL."
