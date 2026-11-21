# BOTW Save Sync — Cemu (PC) ↔ Nintendo Switch

Набор из двух программ с графическим интерфейсом для автоматической синхронизации и конвертации сохранений *The Legend of Zelda: Breath of the Wild* между ПК (эмулятор Cemu) и прошитой Nintendo Switch — без отдельного JKSV и FTPD.

Готовые к запуску файлы (`BOTWSaveSync.nro` для Switch и `botw_save_sync.py` для ПК) уже загружены в корень основной ветки (`main`), поэтому ничего собирать вручную не нужно.

## Структура репозитория

```text
botw-save-sync/
├── BOTWSaveSync.nro             # Готовое homebrew-приложение для Nintendo Switch (v2.2)
├── botw_save_sync.py            # Готовая программа для ПК с GUI (Python 3, без зависимостей)
├── botw_sync_config.json        # Файл конфигурации ПК-программы
├── main.c                       # Актуальный исходный код Switch homebrew (v2.2)
├── pc/
│   └── botw_save_sync.py        # Копия ПК-программы в папке исходников
├── switch/
│   ├── Makefile                 # Сборка BOTWSaveSync.nro из исходников (devkitA64 + libnx)
│   └── source/
│       └── main.c               # Исходный код для сборки в GitHub Actions / devkitPro
└── .github/
    └── workflows/
        └── build.yml            # Автоматическая пересборка BOTWSaveSync.nro в GitHub Actions
```

## Возможности

### На Nintendo Switch (`BOTWSaveSync.nro` v2.2)

* **Встроенный FTP-сервер (порт `5000`):** запускается автоматически при открытии приложения и показывает текущий IP-адрес Switch в шапке экрана. Отдельный FTPD или `sys-ftpd` не требуются.
* **Прямая работа с сохранениями BOTW:** монтирует сохранение игры (`01007EF00011E000`) напрямую через [libnx `fs_dev`](https://switchbrew.github.io/libnx/fs__dev_8h.html) и коммитит журнал `save:/` после каждого файла, исключая ошибки переполнения журнала.
* **HD-интерфейс 1280×720:** работает с кнопками Joy-Con / Pro Controller и с сенсорным экраном.
* **Страховочный бэкап и откат:** перед каждым импортом старое сохранение автоматически копируется в `sdmc:/switch/botw_save_sync/backup_last` с возможностью мгновенного отката кнопкой **X**.

### На ПК (`botw_save_sync.py`)

* **Автоконвертация Wii U ↔ Switch:** переводит `.sav` файлы (`option.sav`, `caption.sav`, `game_data.sav`, `trackblock*.sav`) между Big Endian (Wii U / Cemu) и Little Endian (Switch) по спецификации [ZeldaMods Wiki](https://zeldamods.org/wiki/Save_Files), сохраняя UTF-8 строки и снимки альбома.
* **Авто-совместимость версий:** при переносе со Switch (1.6.0+, `0x471E`) на Cemu (1.5.0, `0x471B`) автоматически корректирует версию заголовка.
* **Умная синхронизация:** сравнивает дату изменения файлов на ПК и Switch и сама выбирает направление переноса (либо ручной режим **Cemu → Switch** / **Switch → Cemu**).
* **Два режима передачи:** по Wi-Fi через встроенный FTP или через локальную папку (USB DBI MTP / SD-карта).
* **Резервные копии:** перед каждой заменой создаёт копию в папке `backups/` рядом со скриптом.

## Быстрая установка (из готовых файлов в ветке `main`)

### 1. Установка на Nintendo Switch
1. Скачайте готовый файл `BOTWSaveSync.nro` из корня репозитория.
2. Скопируйте его на SD-карту Switch по пути:
   ```text
   SD:\switch\botw_save_sync\BOTWSaveSync.nro
   ```
3. Запустите *Breath of the Wild* на Switch хотя бы один раз под нужным профилем до главного меню и закройте игру (чтобы система создала контейнер сохранения).

### 2. Настройка программы на ПК (один раз)
1. Скачайте `botw_save_sync.py` из корня репозитория и запустите его (нужен Python 3, дополнительные пакеты не требуются):
   ```bash
   python botw_save_sync.py
   ```
2. Откройте **Homebrew Menu → BOTW Save Sync** на Switch и посмотрите IP-адрес в зелёной плашке сверху (`FTP ON: 192.168.x.x:5000`).
3. В программе на ПК нажмите **Автопоиск** для папки Cemu (или выберите вручную папку с `option.sav`).
4. Введите IP-адрес Switch, порт `5000` и путь `/JKSV/The Legend of Zelda Breath of the Wild/Sync`. Нажмите **Проверить FTP** и **Сохранить настройки**.

## Ежедневная синхронизация

### Перенос со Switch на ПК (Cemu)
1. Сохранитесь и закройте игру на Switch.
2. Откройте **BOTW Save Sync** на Switch, выберите профиль и нажмите **A** (или коснитесь **EXPORT TO PC**).
3. Не закрывая приложение на Switch, нажмите **Умная синхронизация** (или **Switch → Cemu**) на ПК.
4. Запускайте Cemu.

### Перенос с ПК (Cemu) на Switch
1. Сохранитесь и закройте Cemu на ПК.
2. Откройте **BOTW Save Sync** на Switch (встроенный FTP запустится сам).
3. На ПК нажмите **Умная синхронизация** (или **Cemu → Switch**) и дождитесь завершения передачи.
4. На Switch выберите профиль и нажмите **Y** (или коснитесь **IMPORT FROM PC**). После статуса `Import OK!` запускайте игру.

## Управление и пути

### Управление в `BOTWSaveSync.nro`

| Кнопка / касание | Действие |
| --- | --- |
| `Вверх` / `Вниз` или тап по профилю | Выбор профиля пользователя Switch |
| `A` или **EXPORT TO PC** | Выгрузить сохранение BOTW в папку `Sync` на SD-карте |
| `Y` или **IMPORT FROM PC** | Сделать автобэкап и записать сохранение из `Sync` в игру |
| `X` или **ROLLBACK LAST BACKUP** | Откатиться на предыдущий страховочный бэкап |
| `+` | Выход в Homebrew Menu |

### Справочник путей и Title ID

| Назначение | Путь / идентификатор |
| --- | --- |
| Cemu — USA | `mlc01\usr\save\00050000\101c9400\user\80000001` |
| Cemu — EUR / JPN | `...101c9500...` / `...101c9300...` ([Cemu Wiki](https://wiki.cemu.info/wiki/The_Legend_of_Zelda:_Breath_of_the_Wild)) |
| Nintendo Switch Title ID | `01007EF00011E000` |
| Папка обмена на SD-карте | `sdmc:/JKSV/The Legend of Zelda Breath of the Wild/Sync` |
| Бэкапы на Switch | `sdmc:/switch/botw_save_sync/backup_last` |
| Бэкапы на ПК | `backups/` рядом с `botw_save_sync.py` |

## Пересборка `BOTWSaveSync.nro` из исходников (если меняли код)

* **Через GitHub Actions:** после коммита изменений в `switch/source/main.c` откройте вкладку **Actions** → **Build BOTWSaveSync.nro** и скачайте обновлённый `BOTWSaveSync.nro` из блока **Artifacts**.
* **Локально (devkitPro):** выполните `make` внутри папки `switch/`.

## Важные примечания и решение проблем

* **DLC:** набор установленных дополнений (Expansion Pass) должен совпадать на Switch и в Cemu.
* **Моды:** графические паки Cemu (FPS++, разрешение) полностью совместимы, а моды, добавляющие новые предметы или квесты, могут сделать сохранение несовместимым со Switch.
* **`Mount failed` на Switch:** закройте BOTW полностью, если игра открыта, либо запустите её один раз до главного меню под выбранным профилем.
* **Не подключается FTP:** убедитесь, что ПК и Switch в одной Wi-Fi сети, а IP в ПК-программе совпадает с адресом в верхней плашке `BOTW Save Sync`.
* **Откат:** если импорт пошёл не так, нажмите **X** в `BOTW Save Sync` на Switch или восстановите предыдущую папку из `backups/` на ПК.
