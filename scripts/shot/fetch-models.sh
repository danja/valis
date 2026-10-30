#!/usr/bin/env bash
# scripts/shot/fetch-models.sh
#
# Downloads the neural models behind `valis-shot listen` into $VALIS_MODELS (default
# ~/.local/share/valis/models). Never run by a build or a test: the weights are about 1.1 GB, so
# fetching them is something you choose to do. Each file is pinned to a commit and checked against
# its SHA-256, and a file that is already present and correct is left alone.
#
#   CLAP, laion/larger_clap_general (Apache-2.0), ONNX export by Xenova:
#     https://huggingface.co/Xenova/larger_clap_general
#   AST, MIT/ast-finetuned-audioset-10-10-0.4593 (BSD-3-Clause), ONNX export by Xenova:
#     https://huggingface.co/Xenova/ast-finetuned-audioset-10-10-0.4593
set -euo pipefail

root="${VALIS_MODELS:-$HOME/.local/share/valis/models}"
clap_rev=d78c3994912c441f0151a5cc44d86f6b7bf4c861
ast_rev=249a1fbf0286b40e7f1ed687a8ae396997bf7dc6

# directory | url | sha256
files=(
  "clap|https://huggingface.co/Xenova/larger_clap_general/resolve/$clap_rev/onnx/audio_model.onnx|6192254e7f5c9a921dfd095105216c31ef70f508f910f3b926b7fc373b41f770"
  "clap|https://huggingface.co/Xenova/larger_clap_general/resolve/$clap_rev/onnx/text_model.onnx|386413c758300d8f2be107b9fefbc049945b1b1b302938b01039af0ad4984595"
  "clap|https://huggingface.co/Xenova/larger_clap_general/resolve/$clap_rev/vocab.json|"
  "clap|https://huggingface.co/Xenova/larger_clap_general/resolve/$clap_rev/merges.txt|"
  "clap|https://huggingface.co/Xenova/larger_clap_general/resolve/$clap_rev/preprocessor_config.json|"
  "ast|https://huggingface.co/Xenova/ast-finetuned-audioset-10-10-0.4593/resolve/$ast_rev/onnx/model.onnx|6ff353b1e809da25f900b54494fd4ac12a6fc7db1e25195e238f21be125bb4bd"
  "ast|https://huggingface.co/Xenova/ast-finetuned-audioset-10-10-0.4593/resolve/$ast_rev/config.json|"
)

for entry in "${files[@]}"; do
  IFS='|' read -r dir url sha <<<"$entry"
  name="$(basename "$url")"
  target="$root/$dir/$name"
  mkdir -p "$root/$dir"
  if [[ -f "$target" && ( -z "$sha" || "$(sha256sum "$target" | cut -d' ' -f1)" == "$sha" ) ]]; then
    echo "have  $dir/$name"
    continue
  fi
  echo "fetch $dir/$name"
  curl -fL --retry 3 -o "$target.part" "$url"
  if [[ -n "$sha" && "$(sha256sum "$target.part" | cut -d' ' -f1)" != "$sha" ]]; then
    echo "checksum mismatch for $name; not installed" >&2
    rm -f "$target.part"
    exit 1
  fi
  mv "$target.part" "$target"
done
echo "models are in $root"
