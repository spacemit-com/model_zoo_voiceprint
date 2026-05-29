#!/usr/bin/env bash
set -euo pipefail

module_dir="components/model_zoo/voiceprint"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/voiceprint-pr-test.XXXXXX")"
trap 'rm -rf "${build_dir}"' EXIT

pkg-config --exists libcurl
read -r -a curl_flags <<< "$(pkg-config --cflags --libs libcurl)"

g++ -std=c++17 -Wall -Wextra -Werror \
  "${module_dir}/tests/voiceprint_pr_contract_test.cpp" \
  "${module_dir}/src/vp_presets.cpp" \
  "${module_dir}/src/vp_backend_factory.cpp" \
  "${module_dir}/src/vp_engine.cpp" \
  "${module_dir}/src/wave_reader.cpp" \
  -I"${module_dir}/include" \
  -I"${module_dir}/src" \
  "${curl_flags[@]}" \
  -o "${build_dir}/voiceprint_pr_contract_test"

"${build_dir}/voiceprint_pr_contract_test" --invalid-input-error-path
