# Обзор проекта

**Кратко:** открытая реимплементация движка «Казаки II: Битва за Европу», работающая с данными оригинальной игры.

## Архитектура

Проект на стадии исследования: код движка ещё не написан.

```mermaid
flowchart LR
    exe[engine.exe из копии игры] --> ghidra[Ghidra, headless-анализ]
    ghidra --> re["re/ (локально): декомпилированный код"]
    re --> docs["docs/: форматы и поведение"]
    docs --> src["src/: реимплементация"]
    data[данные игры: .g16, .g17, .g2d, ...] --> src
```

## Структура каталогов

```
opencossacks-2/
├── README.md               — описание проекта
├── OVERVIEW.md             — этот файл
├── CHANGELOG.md            — история изменений
├── docs/                   — заметки о форматах и поведении движка
├── CMakeLists.txt          — сборка (C++20); версия читается из src/core/version.hpp
├── CMakePresets.json       — пресеты: linux (Ninja), windows-msvc (Visual Studio 2022)
├── src/
│   ├── main.cpp            — точка входа, ключ --version
│   └── core/version.hpp    — единственный источник версии и названия продукта
├── tools/ghidra_scripts/
│   ├── ExportDecomp.java   — выгрузка декомпилированного C-кода и индекса функций
│   ├── RunAnalysis.java    — автоанализ с замером времени анализаторов и отменой по флагу
│   ├── Coverage.java       — отчёт о покрытии кода функциями
│   └── SelectiveFix.java   — выборочный доанализ проблемных функций
└── re/                     — локально, в .gitignore: проект Ghidra, логи, вывод
```

## Основные модули

- `tools/ghidra_scripts/ExportDecomp.java`: скрипт Ghidra, записывает `functions.tsv` (адрес, размер, имя) и `engine.c` (декомпилированный код всех функций).

## Сборка и запуск

```sh
cmake --preset linux
cmake --build --preset linux-debug
./build/linux/Debug/opencossacks2 --version
```

## Запуск анализа в Ghidra

```sh
analyzeHeadless re/project engine \
  -import <путь к игре>/app/engine.exe \
  -scriptPath tools/ghidra_scripts -postScript ExportDecomp.java re/decomp \
  -analysisTimeoutPerFile 7200
```

## С чего начать и подводные камни

- Ghidra требует полноценный JDK 21 (не только JRE); путь задаётся в `support/launch.properties` → `JAVA_HOME_OVERRIDE`.
- `engine.exe` (GOG-сборка) импортирует `X3D8.DLL` вместо `d3d8.dll`: это GOG-прокладка, которая просто загружает системный `d3d8.dll` и делает паузу 500 мс после `CreateDevice`.
- Сборка собрана с инкрементальной компоновкой MSVC (секция `.textbss`), поэтому в ней много функций-переходников.
- Ничего из `re/` и файлов игры не коммитить.
