# svt-linux

Нативный 1-в-1 порт **[jmagine/svt-rs](https://github.com/jmagine/svt-rs/releases/tag/v1.0.3)** (`v1.0.3`) под Linux на **C++20 + GTK3**.

Работает нативно без Wine, сохраняет идентичную математику расчётов (`f32`), верстку `300x350`, всплывающие подсказки (tooltips), Drag-and-Drop `.osu` файлов и формат конфига `svt_config.txt` (при запуске из меню приложений конфиг и `backup.osu` хранятся в `~/.config/svt/`).

## Установка в меню приложений (1 команда)
Чтобы `SVT` появился в меню приложений (Rofi / Wofi / KDE / GNOME) со своей иконкой:
```bash
git clone https://github.com/Dekovner/svt-linux.git
cd svt-linux
./install.sh
```
*(Удалить из меню приложений можно командой `make uninstall`).*

## Просто запуск без установки
```bash
chmod +x ./svt
./svt
```

## Сборка из исходников
Требуются `g++` (C++20) и заголовки `gtk+-3.0`:
```bash
make
make install
```

## Лицензия
Распространяется под лицензией [MIT](LICENSE). Оригинальный проект: [jmagine/svt-rs](https://github.com/jmagine/svt-rs) (Copyright (c) 2021 Jason Ma).
