# List-Manager

Tiny native Win32 list manager. One C++ file, one `.exe`, no dependencies.

EN | [RU](#с-ru)

## Build

MinGW-w64 (MSYS2) with `g++` in `PATH`.

```sh
git clone https://github.com/MN-107/List-Manager.git
cd List-Manager
./build.bat
```

`build.bat` builds `list_manager.exe`, then extracts the test suite embedded in its own tail, builds `tests.exe` and runs it — a failing test fails the build.

Application only:

```sh
g++ -O2 -municode -mwindows list_manager.cpp -o list_manager.exe -static -luser32 -lgdi32
```

## Usage

Sheets are shown two at a time; use `<` / `>` to flip pages. `add list` / `del list` manage sheets. Inside a sheet: `add`, `edit`, `delete`, `rename` (sheet name), `tag`, `up` / `down`, and `comment` — a free-form note per item (up to 1024 characters). The window has a fixed 880×480 client size.

## Files

`save` writes `saves/<name>.list` next to the executable, `save txt` writes `saves/<name>.txt`, `load` reads any `.list` from there. The `saves/` folder is created on first run and is git-ignored, so a fresh clone has none of your data in it.

UTF-8 throughout, zero-based indexes, inclusive ranges:

```
[TITLE1]
groceries
[LIST1]
milk
bread
[TAGS1]
fridge:0
[COMMENTS1]
0:2%
```

- `[TITLEn]` — sheet name
- `[LISTn]` — items, one per line
- `[TAGSn]` — optional, `name:index[,index]`, index is a number or a range (`0-3,5`)
- `[COMMENTSn]` — optional, `index:text`, a note attached to one item

`save txt` flattens the same data, tags and comments inlined into each line:

```
groceries
milk (fridge)  // 2%
bread
```

Unknown sections and lines outside any section are ignored, so files without `[TITLE…]` still load.

## License

MIT — see [LICENSE](LICENSE).

---

# С RU

Крошечный нативный менеджер списков для Windows. Один файл C++, один `.exe`, без зависимостей.

## Сборка

MinGW-w64 (MSYS2) с `g++` в `PATH`.

```sh
git clone https://github.com/MN-107/List-Manager.git
cd List-Manager
./build.bat
```

`build.bat` собирает `list_manager.exe`, затем извлекает встроенный в его хвост набор тестов, собирает `tests.exe` и запускает — упавший тест роняет сборку.

Только приложение:

```sh
g++ -O2 -municode -mwindows list_manager.cpp -o list_manager.exe -static -luser32 -lgdi32
```

## Использование

Листы показываются по два за раз; `<` / `>` листают страницы. `add list` / `del list` управляют листами. Внутри листа: `add`, `edit`, `delete`, `rename` (имя листа), `tag`, `up` / `down` и `comment` — произвольная заметка к пункту (до 1024 символов). Размер окна фиксирован — 880×480 по клиентской области.

## Файлы

`save` пишет `saves/<name>.list` рядом с исполняемым файлом, `save txt` пишет `saves/<name>.txt`, `load` читает любой `.list` оттуда. Папка `saves/` создаётся при первом запуске и игнорируется git, поэтому в свежем клоне ваших данных в ней нет.

Везде UTF-8, нумерация с нуля, диапазоны включительные:

```
[TITLE1]
groceries
[LIST1]
milk
bread
[TAGS1]
fridge:0
[COMMENTS1]
0:2%
```

- `[TITLEn]` — имя листа
- `[LISTn]` — пункты, по одному в строке
- `[TAGSn]` — необязательно, `имя:индекс[,индекс]`, индекс — число или диапазон (`0-3,5`)
- `[COMMENTSn]` — необязательно, `индекс:текст`, заметка к пункту

`save txt` пишет те же данные плоско, теги и заметки встроены в строку пункта:

```
groceries
milk (fridge)  // 2%
bread
```

Неизвестные секции и строки вне секций игнорируются, поэтому файлы без `[TITLE…]` тоже загружаются.

## Лицензия

MIT — см. [LICENSE](LICENSE).