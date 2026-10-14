#!/usr/bin/env bash
# Запуск OpenCossacks 2 на данных оригинальной игры.
# Использование: ./run.sh [ключи движка...]
# Переменные: GAME_DATA — каталог app оригинала, OC2_BIN — путь к собранному exe.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
data="${GAME_DATA:-$HOME/Games/Cossacks/c2_battle_for_europe/app}"

bin="${OC2_BIN:-}"
if [ -z "$bin" ]; then
    for cand in "$root/build/linux/Release/opencossacks2" "$root/build/linux/Debug/opencossacks2" /usr/bin/opencossacks2; do
        [ -x "$cand" ] && { bin="$cand"; break; }
    done
fi
[ -n "$bin" ] && [ -x "$bin" ] || { echo "Не найден opencossacks2: соберите (cmake --preset linux && cmake --build --preset linux-debug) или задайте OC2_BIN" >&2; exit 1; }
[ -d "$data" ] || { echo "Нет каталога данных игры: $data (задайте GAME_DATA)" >&2; exit 1; }

exec "$bin" --data "$data" "$@"
