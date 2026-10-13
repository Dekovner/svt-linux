# svt-linux

Нативный 1-в-1 порт **[jmagine/svt-rs](https://github.com/jmagine/svt-rs/releases/tag/v1.0.3)** (`v1.0.3`) под Linux на **C++20 + GTK3**.

Работает нативно без Wine, сохраняет идентичную математику расчётов (`f32`), верстку `300x350`, всплывающие подсказки (tooltips), Drag-and-Drop `.osu` файлов и формат конфига `svt_config.txt`.

## Возможности
- **Режимы интерполяции**: `Lin. SV`, `Exp. SV`, `Pol. SV`, `Sin. SV`, `Flat SV` (`Mult. Scaling`), `Lin. Vol` (включая фикс `v1.0.3` для `Flat SV` масштабирования по BPM на секциях с красными линиями).
- **Объекты**: `Hits`, `Snaps` (произвольный делитель `X / Y Snapping` + 25% после последней ноты для фейдаутов), `Inh. lines`.
- **Доп. опции**: `Offset`, `Buffer`, `Min. Spacing`, `Ignore BPM`, `Preview Diff` (`[preview].osu`), авто-бэкап в `backup.osu` и мгновенный `Undo` через атомарный обмен файлов (`renameat2` / `RENAME_EXCHANGE`).

## Запуск готового бинарника
```bash
chmod +x ./svt
./svt
```

## Сборка из исходников
Требуются `g++` (с поддержкой C++20) и заголовки `gtk+-3.0`:
```bash
make
```

## Лицензия
Распространяется под лицензией [MIT](LICENSE). Оригинальный проект: [jmagine/svt-rs](https://github.com/jmagine/svt-rs) (Copyright (c) 2021 Jason Ma).
