#!/bin/sh
# Компилирует шейдеры из shaders/ в SPIR-V и записывает их как массивы C++
# в src/render/shaders/. Результат хранится в репозитории, поэтому для сборки
# компилятор шейдеров не нужен; CI проверяет, что заголовки не устарели.
# Нужен glslangValidator (пакет glslang-tools).
set -eu
cd "$(dirname "$0")/.."
for src in shaders/*.vert shaders/*.frag; do
    base=$(basename "$src")            # sprite.vert
    stem=${base%.*}                    # sprite
    stage=${base##*.}                  # vert
    name=k$(echo "$stem" | sed 's/^./\U&/')$(echo "$stage" | sed 's/^./\U&/') # kSpriteVert
    glslangValidator -V --target-env vulkan1.2 --quiet --vn "$name" \
        -o "src/render/shaders/${stem}_${stage}.h" "$src"
done
