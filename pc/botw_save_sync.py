#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
BOTW Save Sync — программа с графическим интерфейсом для автоматической
синхронизации и конвертации сохранений The Legend of Zelda: Breath of the Wild
между ПК (Cemu / Wii U) и прошитой Nintendo Switch (через FTP или локальную папку).

Зависимости: только стандартная библиотека Python 3 (tkinter, ftplib, struct, json, shutil).
"""

import ftplib
import io
import json
import os
import posixpath
import shutil
import struct
import threading
import time
import zlib
from datetime import datetime
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox, scrolledtext, ttk


APP_TITLE = "BOTW Save Sync — Cemu ↔ Nintendo Switch"
CONFIG_FILE = Path(__file__).resolve().parent / "botw_sync_config.json"
BACKUP_ROOT = Path(__file__).resolve().parent / "backups"

# Известные Title ID для Breath of the Wild на Wii U / Cemu (USA, EUR, JPN)
CEMU_TITLE_IDS = ("101c9400", "101c9500", "101c9300")

# Известные версии сохранений BOTW (заголовок 0x00)
SAVE_VERSIONS = {
    0x24E2: "1.0.0",
    0x24EE: "1.1.0–1.1.2",
    0x2588: "1.2.0",
    0x29C0: "1.3.0",
    0x2A46: "1.3.1",
    0x3EF8: "1.3.3",
    0x3EF9: "1.3.4",
    0x471A: "1.4.0–1.4.1",
    0x471B: "1.5.0 (Wii U последняя)",
    0x471E: "1.6.0+ (Switch)",
}

# Ключи строковых параметров в .sav файлах BOTW:
# их 4-байтовые блоки данных содержат UTF-8 символы и НЕ должны переворачиваться по байтам.
STRING_KEY_NAMES = [
    "PorchItem",
    "Horse_UserName",
    "Horse_Name",
    "LastSaveTime_String",
    "SaveDistrictName",
    "SaveLocationName",
    "CaptionPictSize",
    "EquipStand_Sword_Name",
    "EquipStand_Bow_Name",
    "EquipStand_Shield_Name",
    "PictureBook_Name",
    "Album_PictureName",
    "CurrentMapName",
    "CurrentLocationName",
    "PlayerName",
    "AmiiboLastUseTime",
    "WarpPoint_Name",
    "CustomMapPin_Name",
    "Korok_Name",
    "Quest_CurrentStepName",
    "MiniGame_BestRecordName",
    "Cook_LastItemName",
    "Shop_LastItemName",
    "Weapon_ModifierName",
    "Bow_ModifierName",
    "Shield_ModifierName",
    "SeekerTower_Name",
    "Dungeon_Name",
]
# Добавляем пронумерованные/массивные варианты строковых ключей, встречающиеся в BOTW
for _prefix in ("EquipStand_Sword", "EquipStand_Bow", "EquipStand_Shield", "Horse_UserName"):
    for _i in range(10):
        STRING_KEY_NAMES.append(f"{_prefix}_{_i}")

KNOWN_STRING_HASHES = {zlib.crc32(name.encode("ascii")) & 0xFFFFFFFF for name in STRING_KEY_NAMES}


def detect_save_platform(sav_bytes: bytes) -> tuple[str, int]:
    """
    Определяет платформу ('wiiu' или 'switch') и версию сохранения по 12-байтовому заголовку.
    Формат заголовка:
      0x00 (4 байта): version
      0x04 (4 байта): marker (0xFFFFFFFF)
      0x08 (4 байта): unknown (всегда 0x1)
    """
    if len(sav_bytes) < 12:
        raise ValueError("Файл сохранения слишком мал (< 12 байт).")

    be_ver, be_mark, be_unk = struct.unpack(">III", sav_bytes[:12])
    le_ver, le_mark, le_unk = struct.unpack("<III", sav_bytes[:12])

    if be_mark == 0xFFFFFFFF and be_unk == 1:
        return "wiiu", be_ver
    if le_mark == 0xFFFFFFFF and le_unk == 1:
        return "switch", le_ver

    raise ValueError(
        f"Неизвестный формат заголовка .sav (BE={be_ver:#x}/{be_unk}, LE={le_ver:#x}/{le_unk})."
    )


def _looks_like_utf8_chunk_sequence(chunks: list[bytes]) -> bool:
    """
    Дополнительная эвристика для неизвестных строковых массивов:
    проверяет, является ли последовательность 4-байтовых значений с одинаковым ID
    корректной нуль-терминированной ASCII/UTF-8 строкой (например, ID предметов вроде Weapon_Sword_001).
    """
    if len(chunks) < 4:
        return False
    raw = b"".join(chunks)
    # Должно начинаться с печатаемых ASCII-символов и заканчиваться нулевыми байтами
    stripped = raw.rstrip(b"\x00")
    if not stripped or len(stripped) == len(raw):
        return False
    # Проверяем, что все байты до нулей — печатаемый ASCII (буквы, цифры, '_', '-')
    printable = all(32 <= b <= 126 for b in stripped)
    has_alpha = any(65 <= b <= 90 or 97 <= b <= 122 for b in stripped)
    return printable and has_alpha and b"\x00" not in stripped


def convert_sav_bytes(
    data: bytes,
    target_platform: str,
    downgrade_to_150: bool = False,
) -> tuple[bytes, str, int]:
    """
    Конвертирует содержимое .sav файла (option.sav, caption.sav, game_data.sav)
    между форматами Wii U (Big Endian) и Switch (Little Endian).
    Возвращает: (новые_байты, исходная_платформа, версия_сохранения).
    """
    src_platform, version = detect_save_platform(data)
    if src_platform == target_platform:
        # Если платформа уже целевая, при необходимости только патчим версию
        out = bytearray(data)
        if target_platform == "wiiu" and downgrade_to_150 and version > 0x471B:
            struct.pack_into(">I", out, 0, 0x471B)
        return bytes(out), src_platform, version

    src_endian = ">" if src_platform == "wiiu" else "<"
    dst_endian = ">" if target_platform == "wiiu" else "<"

    out = bytearray(len(data))

    # 1. Заголовок (12 байт: 3 uint32)
    ver, marker, unk = struct.unpack_from(f"{src_endian}III", data, 0)
    if target_platform == "wiiu" and downgrade_to_150 and ver > 0x471B:
        ver = 0x471B
    struct.pack_into(f"{dst_endian}III", out, 0, ver, marker, unk)

    # 2. Тело файла: 8-байтовые чанки (4 байта ID + 4 байта Value)
    offset = 12
    total_len = len(data)
    # Выравнивание до 8 байт
    limit = total_len - ((total_len - 12) % 8)

    # Предварительно сгруппируем подряд идущие чанки с одинаковым ID для эвристики строк
    i = offset
    while i < limit:
        chunk_id_bytes = data[i : i + 4]
        chunk_id = struct.unpack(f"{src_endian}I", chunk_id_bytes)[0]

        # Находим конец серии подряд идущих чанков с тем же ID
        j = i + 8
        while j < limit and data[j : j + 4] == chunk_id_bytes:
            j += 8

        count = (j - i) // 8
        is_string = chunk_id in KNOWN_STRING_HASHES
        if not is_string and count >= 8:
            val_chunks = [data[k + 4 : k + 8] for k in range(i, j, 8)]
            if _looks_like_utf8_chunk_sequence(val_chunks):
                is_string = True

        # Записываем серию чанков
        for k in range(i, j, 8):
            struct.pack_into(f"{dst_endian}I", out, k, chunk_id)
            if is_string:
                # Строковые данные не переворачиваем
                out[k + 4 : k + 8] = data[k + 4 : k + 8]
            else:
                val_u32 = struct.unpack_from(f"{src_endian}I", data, k + 4)[0]
                struct.pack_into(f"{dst_endian}I", out, k + 4, val_u32)

        i = j

    # Хвостовые байты (если есть) копируем без изменений
    if limit < total_len:
        out[limit:] = data[limit:]

    return bytes(out), src_platform, version


def convert_trackblock_bytes(data: bytes) -> bytes:
    """
    Конвертирует файлы Hero's Path (tracker/trackblockXX.sav):
    переворачивает каждые 4 байта (uint32 Big <-> Little Endian).
    """
    out = bytearray(len(data))
    limit = len(data) - (len(data) % 4)
    for off in range(0, limit, 4):
        out[off : off + 4] = data[off : off + 4][::-1]
    if limit < len(data):
        out[limit:] = data[limit:]
    return bytes(out)


def convert_save_directory(
    src_dir: Path,
    dst_dir: Path,
    target_platform: str,
    downgrade_to_150: bool = False,
    log_cb=None,
) -> dict:
    """
    Конвертирует всю папку сохранения BOTW (option.sav, слоты 0..8, tracker, album, pict_book)
    из src_dir в dst_dir.
    """
    if not (src_dir / "option.sav").is_file():
        raise FileNotFoundError(f"В папке {src_dir} не найден файл option.sav.")

    opt_bytes = (src_dir / "option.sav").read_bytes()
    src_platform, save_ver = detect_save_platform(opt_bytes)
    need_trackblock_swap = src_platform != target_platform

    if dst_dir.exists():
        shutil.rmtree(dst_dir)
    dst_dir.mkdir(parents=True, exist_ok=True)

    converted_sav = 0
    copied_other = 0

    for root, _, files in os.walk(src_dir):
        rel_root = Path(root).relative_to(src_dir)
        out_root = dst_dir / rel_root
        out_root.mkdir(parents=True, exist_ok=True)

        for fname in files:
            src_file = Path(root) / fname
            dst_file = out_root / fname
            lower = fname.lower()

            if lower in ("option.sav", "caption.sav", "game_data.sav"):
                raw = src_file.read_bytes()
                conv, _, _ = convert_sav_bytes(raw, target_platform, downgrade_to_150)
                dst_file.write_bytes(conv)
                converted_sav += 1
            elif lower.startswith("trackblock") and lower.endswith(".sav"):
                raw = src_file.read_bytes()
                dst_file.write_bytes(convert_trackblock_bytes(raw) if need_trackblock_swap else raw)
                converted_sav += 1
            else:
                shutil.copy2(src_file, dst_file)
                copied_other += 1

    if log_cb:
        ver_str = SAVE_VERSIONS.get(save_ver, f"{save_ver:#x}")
        log_cb(
            f"Конвертация завершена: {src_platform.upper()} → {target_platform.upper()} "
            f"(версия {ver_str}, .sav файлов: {converted_sav}, прочих: {copied_other})."
        )

    return {
        "src_platform": src_platform,
        "target_platform": target_platform,
        "version": save_ver,
        "converted_sav": converted_sav,
        "copied_other": copied_other,
    }


def get_folder_latest_mtime(folder: Path) -> float:
    """Возвращает максимальное время модификации среди файлов .sav в папке."""
    latest = 0.0
    if not folder.exists():
        return latest
    for p in folder.rglob("*.sav"):
        try:
            m = p.stat().st_mtime
            if m > latest:
                latest = m
        except OSError:
            pass
    return latest


class SwitchFTPClient:
    """Обёртка над ftplib.FTP для скачивания/загрузки папки сохранений на прошитую Switch (ftpd)."""

    def __init__(self, host: str, port: int = 5000, user: str = "", password: str = "", timeout: int = 15):
        self.host = host.strip()
        self.port = int(port)
        self.user = user
        self.password = password
        self.timeout = timeout
        self.ftp: ftplib.FTP | None = None

    def __enter__(self):
        self.ftp = ftplib.FTP()
        self.ftp.connect(self.host, self.port, timeout=self.timeout)
        self.ftp.login(self.user or "anonymous", self.password or "anonymous@")
        self.ftp.set_pasv(True)
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if self.ftp:
            try:
                self.ftp.quit()
            except Exception:
                try:
                    self.ftp.close()
                except Exception:
                    pass

    def list_dir(self, remote_path: str) -> list[tuple[str, bool, float]]:
        """
        Возвращает список (имя, is_dir, mtime_ts) для remote_path.
        Поддерживает MLSD (современный ftpd) с откатом на LIST.
        """
        assert self.ftp is not None
        entries = []
        try:
            for name, facts in self.ftp.mlsd(remote_path):
                if name in (".", ".."):
                    continue
                is_dir = facts.get("type", "").lower() == "dir"
                mtime = 0.0
                if "modify" in facts:
                    try:
                        dt = datetime.strptime(facts["modify"][:14], "%Y%m%d%H%M%S")
                        mtime = dt.timestamp()
                    except ValueError:
                        pass
                entries.append((name, is_dir, mtime))
            return entries
        except ftplib.error_perm:
            pass

        lines: list[str] = []
        self.ftp.cwd(remote_path)
        self.ftp.retrlines("LIST", lines.append)
        for line in lines:
            parts = line.split(maxsplit=8)
            if len(parts) < 9:
                continue
            name = parts[8]
            if name in (".", ".."):
                continue
            is_dir = line.startswith("d")
            entries.append((name, is_dir, 0.0))
        return entries

    def get_remote_latest_mtime(self, remote_path: str) -> float:
        """Пытается получить самое свежее время изменения файлов .sav на Switch."""
        latest = 0.0
        try:
            for name, is_dir, mtime in self.list_dir(remote_path):
                full = posixpath.join(remote_path, name)
                if is_dir:
                    sub = self.get_remote_latest_mtime(full)
                    if sub > latest:
                        latest = sub
                elif name.lower().endswith(".sav"):
                    if mtime > latest:
                        latest = mtime
                    elif mtime == 0.0 and self.ftp is not None:
                        try:
                            resp = self.ftp.sendcmd(f"MDTM {full}")
                            if resp.startswith("213 "):
                                dt = datetime.strptime(resp[4:18], "%Y%m%d%H%M%S")
                                latest = max(latest, dt.timestamp())
                        except Exception:
                            pass
        except Exception:
            pass
        return latest

    def download_dir(self, remote_dir: str, local_dir: Path, log_cb=None):
        """Рекурсивно скачивает папку с Switch на ПК."""
        assert self.ftp is not None
        local_dir.mkdir(parents=True, exist_ok=True)
        for name, is_dir, _ in self.list_dir(remote_dir):
            r_path = posixpath.join(remote_dir, name)
            l_path = local_dir / name
            if is_dir:
                self.download_dir(r_path, l_path, log_cb)
            else:
                with open(l_path, "wb") as f:
                    self.ftp.retrbinary(f"RETR {r_path}", f.write)
        if log_cb:
            log_cb(f"Скачано с FTP: {remote_dir}")

    def _ensure_remote_dir(self, remote_dir: str):
        assert self.ftp is not None
        parts = [p for p in remote_dir.split("/") if p]
        cur = "/" if remote_dir.startswith("/") else ""
        for p in parts:
            cur = posixpath.join(cur, p) if cur else p
            try:
                self.ftp.mkd(cur)
            except ftplib.error_perm:
                pass

    def upload_dir(self, local_dir: Path, remote_dir: str, log_cb=None):
        """Рекурсивно загружает папку с ПК на Switch по FTP."""
        assert self.ftp is not None
        self._ensure_remote_dir(remote_dir)
        for item in local_dir.iterdir():
            r_path = posixpath.join(remote_dir, item.name)
            if item.is_dir():
                self.upload_dir(item, r_path, log_cb)
            else:
                with open(item, "rb") as f:
                    self.ftp.storbinary(f"STOR {r_path}", f)
        if log_cb:
            log_cb(f"Загружено на FTP: {remote_dir}")


def auto_discover_cemu_save() -> str | None:
    """Ищет папку сохранений BOTW в стандартных расположениях Cemu на Windows/Linux/macOS."""
    candidates: list[Path] = []
    home = Path.home()

    # Стандартные пути на Windows / Linux / macOS
    appdata = os.environ.get("APPDATA")
    if appdata:
        candidates.append(Path(appdata) / "Cemu" / "mlc01")
    localappdata = os.environ.get("LOCALAPPDATA")
    if localappdata:
        candidates.append(Path(localappdata) / "Cemu" / "mlc01")
    candidates.extend([
        home / ".local" / "share" / "Cemu" / "mlc01",
        home / "Library" / "Application Support" / "Cemu" / "mlc01",
        Path("C:/Cemu/mlc01"),
        Path("D:/Cemu/mlc01"),
        Path("C:/Games/Cemu/mlc01"),
        Path("D:/Games/Cemu/mlc01"),
    ])

    for mlc in candidates:
        for tid in CEMU_TITLE_IDS:
            user_dir = mlc / "usr" / "save" / "00050000" / tid / "user"
            if user_dir.is_dir():
                for slot in sorted(user_dir.iterdir()):
                    if slot.is_dir() and (slot / "option.sav").is_file():
                        return str(slot)
    return None


class BotwSyncApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("780x680")
        self.minsize(700, 600)
        self.configure(bg="#131720")

        self._setup_styles()
        self._build_ui()
        self._load_config()

    def _setup_styles(self):
        style = ttk.Style(self)
        style.theme_use("clam")

        bg = "#131720"
        card = "#1b2230"
        fg = "#e6edf3"
        muted = "#94a3b8"
        accent = "#3b82f6"
        gold = "#d4af37"

        style.configure(".", background=bg, foreground=fg, font=("Segoe UI", 10))
        style.configure("TFrame", background=bg)
        style.configure("Card.TFrame", background=card, relief="flat")
        style.configure("TLabel", background=bg, foreground=fg)
        style.configure("Card.TLabel", background=card, foreground=fg)
        style.configure("Muted.TLabel", background=card, foreground=muted, font=("Segoe UI", 9))
        style.configure("Title.TLabel", background=bg, foreground=gold, font=("Segoe UI", 15, "bold"))
        style.configure("Section.TLabel", background=card, foreground=gold, font=("Segoe UI", 10, "bold"))
        style.configure("TRadiobutton", background=card, foreground=fg)
        style.configure("TCheckbutton", background=card, foreground=fg)

        style.configure(
            "TEntry",
            fieldbackground="#0f141d",
            foreground=fg,
            insertcolor=fg,
            bordercolor="#2e3a4e",
            padding=5,
        )
        style.configure(
            "Accent.TButton",
            background=accent,
            foreground="#ffffff",
            font=("Segoe UI", 10, "bold"),
            padding=(12, 8),
        )
        style.map("Accent.TButton", background=[("active", "#2563eb")])

        style.configure(
            "Secondary.TButton",
            background="#263145",
            foreground=fg,
            padding=(10, 6),
        )
        style.map("Secondary.TButton", background=[("active", "#33415c")])

    def _build_ui(self):
        root = ttk.Frame(self, padding=16)
        root.pack(fill="both", expand=True)

        # Заголовок
        header = ttk.Frame(root)
        header.pack(fill="x", pady=(0, 12))
        ttk.Label(header, text="⚔️ BOTW Save Sync", style="Title.TLabel").pack(side="left")
        ttk.Label(
            header,
            text="Автоматическая синхронизация Cemu ↔ Switch",
            foreground="#94a3b8",
        ).pack(side="left", padx=(12, 0), pady=(4, 0))

        # Блок 1: Путь к Cemu
        cemu_card = ttk.Frame(root, style="Card.TFrame", padding=12)
        cemu_card.pack(fill="x", pady=(0, 10))
        ttk.Label(cemu_card, text="1. Папка сохранений Cemu (ПК)", style="Section.TLabel").pack(
            anchor="w", pady=(0, 6)
        )
        cemu_row = ttk.Frame(cemu_card, style="Card.TFrame")
        cemu_row.pack(fill="x")
        self.cemu_path_var = tk.StringVar()
        ttk.Entry(cemu_row, textvariable=self.cemu_path_var).pack(side="left", fill="x", expand=True)
        ttk.Button(cemu_row, text="Обзор...", style="Secondary.TButton", command=self._browse_cemu).pack(
            side="left", padx=(6, 0)
        )
        ttk.Button(
            cemu_row, text="Автопоиск", style="Secondary.TButton", command=self._find_cemu
        ).pack(side="left", padx=(6, 0))
        ttk.Label(
            cemu_card,
            text="Обычно: ...\\Cemu\\mlc01\\usr\\save\\00050000\\101c9400 (или 101c9500)\\user\\80000001",
            style="Muted.TLabel",
        ).pack(anchor="w", pady=(4, 0))

        # Блок 2: Подключение к Switch
        sw_card = ttk.Frame(root, style="Card.TFrame", padding=12)
        sw_card.pack(fill="x", pady=(0, 10))
        ttk.Label(sw_card, text="2. Прошитая Nintendo Switch (JKSV / FTP / USB)", style="Section.TLabel").pack(
            anchor="w", pady=(0, 6)
        )

        self.mode_var = tk.StringVar(value="ftp")
        mode_row = ttk.Frame(sw_card, style="Card.TFrame")
        mode_row.pack(fill="x", pady=(0, 8))
        ttk.Radiobutton(
            mode_row,
            text="По Wi-Fi через FTP (ftpd / sys-ftpd)",
            variable=self.mode_var,
            value="ftp",
            command=self._toggle_mode,
        ).pack(side="left", padx=(0, 16))
        ttk.Radiobutton(
            mode_row,
            text="Локальная папка (SD-карта / USB DBI MTP)",
            variable=self.mode_var,
            value="local",
            command=self._toggle_mode,
        ).pack(side="left")

        # FTP поля
        self.ftp_frame = ttk.Frame(sw_card, style="Card.TFrame")
        self.ftp_frame.pack(fill="x")

        grid1 = ttk.Frame(self.ftp_frame, style="Card.TFrame")
        grid1.pack(fill="x", pady=(0, 6))
        ttk.Label(grid1, text="IP Switch:", style="Card.TLabel").pack(side="left")
        self.ftp_host_var = tk.StringVar(value="192.168.1.100")
        ttk.Entry(grid1, textvariable=self.ftp_host_var, width=16).pack(side="left", padx=(6, 12))

        ttk.Label(grid1, text="Порт:", style="Card.TLabel").pack(side="left")
        self.ftp_port_var = tk.StringVar(value="5000")
        ttk.Entry(grid1, textvariable=self.ftp_port_var, width=7).pack(side="left", padx=(6, 12))

        ttk.Button(
            grid1, text="Проверить FTP", style="Secondary.TButton", command=self._test_ftp
        ).pack(side="right")

        grid2 = ttk.Frame(self.ftp_frame, style="Card.TFrame")
        grid2.pack(fill="x")
        ttk.Label(grid2, text="Путь JKSV на Switch:", style="Card.TLabel").pack(side="left")
        self.ftp_path_var = tk.StringVar(
            value="/JKSV/The Legend of Zelda Breath of the Wild/Sync"
        )
        ttk.Entry(grid2, textvariable=self.ftp_path_var).pack(
            side="left", fill="x", expand=True, padx=(6, 0)
        )

        # Локальная папка Switch
        self.local_frame = ttk.Frame(sw_card, style="Card.TFrame")
        ttk.Label(self.local_frame, text="Папка сохранения Switch:", style="Card.TLabel").pack(
            side="left"
        )
        self.sw_local_var = tk.StringVar()
        ttk.Entry(self.local_frame, textvariable=self.sw_local_var).pack(
            side="left", fill="x", expand=True, padx=(6, 6)
        )
        ttk.Button(
            self.local_frame,
            text="Обзор...",
            style="Secondary.TButton",
            command=self._browse_switch_local,
        ).pack(side="left")

        # Опции
        opts_row = ttk.Frame(sw_card, style="Card.TFrame")
        opts_row.pack(fill="x", pady=(8, 0))
        self.downgrade_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            opts_row,
            text="Авто-совместимость версий (1.6.0+ → 1.5.0 для Cemu)",
            variable=self.downgrade_var,
        ).pack(side="left")
        self.backup_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            opts_row,
            text="Делать резервные копии перед заменой",
            variable=self.backup_var,
        ).pack(side="right")

        # Блок 3: Кнопки действий
        actions_card = ttk.Frame(root, style="Card.TFrame", padding=12)
        actions_card.pack(fill="x", pady=(0, 10))
        btn_row = ttk.Frame(actions_card, style="Card.TFrame")
        btn_row.pack(fill="x")

        self.btn_auto = ttk.Button(
            btn_row,
            text="🔄 Умная синхронизация (Авто)",
            style="Accent.TButton",
            command=lambda: self._run_async(self._sync_auto),
        )
        self.btn_auto.pack(side="left", fill="x", expand=True, padx=(0, 6))

        self.btn_push = ttk.Button(
            btn_row,
            text="💻 ПК (Cemu) → 🎮 Switch",
            style="Secondary.TButton",
            command=lambda: self._run_async(self._push_cemu_to_switch),
        )
        self.btn_push.pack(side="left", fill="x", expand=True, padx=6)

        self.btn_pull = ttk.Button(
            btn_row,
            text="🎮 Switch → 💻 ПК (Cemu)",
            style="Secondary.TButton",
            command=lambda: self._run_async(self._pull_switch_to_cemu),
        )
        self.btn_pull.pack(side="left", fill="x", expand=True, padx=(6, 0))

        # Блок 4: Журнал и статус
        log_card = ttk.Frame(root, style="Card.TFrame", padding=12)
        log_card.pack(fill="both", expand=True)
        log_top = ttk.Frame(log_card, style="Card.TFrame")
        log_top.pack(fill="x", pady=(0, 6))
        ttk.Label(log_top, text="Журнал операций", style="Section.TLabel").pack(side="left")
        ttk.Button(
            log_top, text="Сохранить настройки", style="Secondary.TButton", command=self._save_config
        ).pack(side="right")
        ttk.Button(
            log_top, text="Открыть бэкапы", style="Secondary.TButton", command=self._open_backups
        ).pack(side="right", padx=(0, 6))

        self.log_box = scrolledtext.ScrolledText(
            log_card,
            height=12,
            bg="#0b0f17",
            fg="#cbd5e1",
            insertbackground="#ffffff",
            font=("Consolas", 9),
            relief="flat",
            state="disabled",
        )
        self.log_box.pack(fill="both", expand=True)

        self._toggle_mode()
        self.log("Готово к работе. Укажите пути Cemu и Switch, затем выберите действие.")

    def _toggle_mode(self):
        if self.mode_var.get() == "ftp":
            self.local_frame.pack_forget()
            self.ftp_frame.pack(fill="x")
        else:
            self.ftp_frame.pack_forget()
            self.local_frame.pack(fill="x")

    def log(self, msg: str):
        ts = datetime.now().strftime("%H:%M:%S")
        line = f"[{ts}] {msg}\n"

        def _append():
            self.log_box.configure(state="normal")
            self.log_box.insert("end", line)
            self.log_box.see("end")
            self.log_box.configure(state="disabled")

        self.after(0, _append)

    def _set_busy(self, busy: bool):
        state = "disabled" if busy else "normal"
        self.after(
            0,
            lambda: [
                self.btn_auto.configure(state=state),
                self.btn_push.configure(state=state),
                self.btn_pull.configure(state=state),
            ],
        )

    def _run_async(self, fn):
        self._save_config(silent=True)

        def _worker():
            self._set_busy(True)
            try:
                fn()
            except Exception as exc:
                self.log(f"❌ ОШИБКА: {exc}")
                self.after(0, lambda: messagebox.showerror("Ошибка синхронизации", str(exc)))
            finally:
                self._set_busy(False)

        threading.Thread(target=_worker, daemon=True).start()

    def _browse_cemu(self):
        path = filedialog.askdirectory(title="Выберите папку сохранения Cemu (содержит option.sav)")
        if path:
            self.cemu_path_var.set(path)

    def _browse_switch_local(self):
        path = filedialog.askdirectory(title="Выберите папку сохранения Switch (JKSV)")
        if path:
            self.sw_local_var.set(path)

    def _find_cemu(self):
        found = auto_discover_cemu_save()
        if found:
            self.cemu_path_var.set(found)
            self.log(f"✅ Найдена папка Cemu: {found}")
        else:
            messagebox.showinfo(
                "Автопоиск Cemu",
                "Папка сохранений BOTW не найдена автоматически. Укажите её вручную через 'Обзор...'.",
            )

    def _open_backups(self):
        BACKUP_ROOT.mkdir(parents=True, exist_ok=True)
        self.log(f"Папка бэкапов: {BACKUP_ROOT}")
        if os.name == "nt":
            os.startfile(str(BACKUP_ROOT))
        else:
            messagebox.showinfo("Папка бэкапов", str(BACKUP_ROOT))

    def _test_ftp(self):
        host = self.ftp_host_var.get().strip()
        port = int(self.ftp_port_var.get().strip() or 5000)
        remote_dir = self.ftp_path_var.get().strip()
        try:
            with SwitchFTPClient(host, port) as ftp:
                entries = ftp.list_dir(remote_dir)
            self.log(f"✅ FTP подключение успешно! Найдено объектов в {remote_dir}: {len(entries)}")
            messagebox.showinfo("FTP", f"Подключение успешно! Объектов в папке: {len(entries)}")
        except Exception as exc:
            self.log(f"❌ Ошибка FTP: {exc}")
            messagebox.showerror("Ошибка FTP", str(exc))

    def _backup_folder(self, src: Path, label: str):
        if not self.backup_var.get() or not src.exists():
            return
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        dst = BACKUP_ROOT / f"{label}_{stamp}"
        shutil.copytree(src, dst)
        self.log(f"💾 Создан бэкап ({label}): {dst.name}")

    def _validate_cemu_path(self) -> Path:
        p = Path(self.cemu_path_var.get().strip())
        if not p.is_dir() or not (p / "option.sav").is_file():
            raise FileNotFoundError(
                "Неверная папка Cemu: в указанной директории не найден файл option.sav."
            )
        return p

    def _push_cemu_to_switch(self):
        cemu_dir = self._validate_cemu_path()
        self.log("▶ Начинаем перенос: ПК (Cemu) → Switch...")
        self._backup_folder(cemu_dir, "cemu_before_push")

        tmp_converted = BACKUP_ROOT / "_tmp_to_switch"
        convert_save_directory(
            cemu_dir,
            tmp_converted,
            target_platform="switch",
            downgrade_to_150=False,
            log_cb=self.log,
        )

        if self.mode_var.get() == "ftp":
            host = self.ftp_host_var.get().strip()
            port = int(self.ftp_port_var.get().strip() or 5000)
            remote_dir = self.ftp_path_var.get().strip()
            with SwitchFTPClient(host, port) as ftp:
                if self.backup_var.get():
                    bkp = BACKUP_ROOT / f"switch_ftp_{datetime.now().strftime('%Y%m%d_%H%M%S')}"
                    try:
                        ftp.download_dir(remote_dir, bkp, log_cb=self.log)
                    except Exception:
                        pass
                ftp.upload_dir(tmp_converted, remote_dir, log_cb=self.log)
        else:
            sw_dir = Path(self.sw_local_var.get().strip())
            if not sw_dir:
                raise ValueError("Укажите локальную папку сохранения Switch.")
            self._backup_folder(sw_dir, "switch_local")
            if sw_dir.exists():
                shutil.rmtree(sw_dir)
            shutil.copytree(tmp_converted, sw_dir)

        shutil.rmtree(tmp_converted, ignore_errors=True)
        self.log("✅ Готово! Теперь в JKSV на Switch нажмите Y (Restore) на выбранном бэкапе.")

    def _pull_switch_to_cemu(self):
        cemu_dir = self._validate_cemu_path()
        self.log("▶ Начинаем перенос: Switch → ПК (Cemu)...")
        self._backup_folder(cemu_dir, "cemu_before_pull")

        tmp_src = BACKUP_ROOT / "_tmp_from_switch"
        if tmp_src.exists():
            shutil.rmtree(tmp_src)

        if self.mode_var.get() == "ftp":
            host = self.ftp_host_var.get().strip()
            port = int(self.ftp_port_var.get().strip() or 5000)
            remote_dir = self.ftp_path_var.get().strip()
            with SwitchFTPClient(host, port) as ftp:
                ftp.download_dir(remote_dir, tmp_src, log_cb=self.log)
        else:
            sw_dir = Path(self.sw_local_var.get().strip())
            if not sw_dir.is_dir() or not (sw_dir / "option.sav").is_file():
                raise FileNotFoundError("В локальной папке Switch не найден файл option.sav.")
            shutil.copytree(sw_dir, tmp_src)

        tmp_converted = BACKUP_ROOT / "_tmp_to_cemu"
        convert_save_directory(
            tmp_src,
            tmp_converted,
            target_platform="wiiu",
            downgrade_to_150=self.downgrade_var.get(),
            log_cb=self.log,
        )

        # Заменяем содержимое папки Cemu
        for item in cemu_dir.iterdir():
            if item.is_dir():
                shutil.rmtree(item)
            else:
                item.unlink()
        for item in tmp_converted.iterdir():
            dst = cemu_dir / item.name
            if item.is_dir():
                shutil.copytree(item, dst)
            else:
                shutil.copy2(item, dst)

        shutil.rmtree(tmp_src, ignore_errors=True)
        shutil.rmtree(tmp_converted, ignore_errors=True)
        self.log("✅ Готово! Сохранение со Switch установлено в Cemu.")

    def _sync_auto(self):
        cemu_dir = self._validate_cemu_path()
        cemu_mtime = get_folder_latest_mtime(cemu_dir)

        if self.mode_var.get() == "ftp":
            host = self.ftp_host_var.get().strip()
            port = int(self.ftp_port_var.get().strip() or 5000)
            remote_dir = self.ftp_path_var.get().strip()
            with SwitchFTPClient(host, port) as ftp:
                sw_mtime = ftp.get_remote_latest_mtime(remote_dir)
        else:
            sw_dir = Path(self.sw_local_var.get().strip())
            sw_mtime = get_folder_latest_mtime(sw_dir)

        if sw_mtime == 0.0 and cemu_mtime == 0.0:
            raise RuntimeError("Не удалось определить дату сохранений ни на ПК, ни на Switch.")

        cemu_str = datetime.fromtimestamp(cemu_mtime).strftime("%Y-%m-%d %H:%M:%S") if cemu_mtime else "нет"
        sw_str = datetime.fromtimestamp(sw_mtime).strftime("%Y-%m-%d %H:%M:%S") if sw_mtime else "нет"
        self.log(f"🕒 Последнее изменение — Cemu: {cemu_str} | Switch: {sw_str}")

        if abs(cemu_mtime - sw_mtime) < 5:
            self.log("ℹ️ Сохранения уже синхронизированы (разница < 5 сек).")
            return

        if cemu_mtime > sw_mtime:
            self.log("➡️ Новее на ПК (Cemu) — отправляем на Switch.")
            self._push_cemu_to_switch()
        else:
            self.log("⬅️ Новее на Switch — скачиваем в Cemu.")
            self._pull_switch_to_cemu()

    def _save_config(self, silent: bool = False):
        cfg = {
            "cemu_path": self.cemu_path_var.get().strip(),
            "mode": self.mode_var.get(),
            "ftp_host": self.ftp_host_var.get().strip(),
            "ftp_port": self.ftp_port_var.get().strip(),
            "ftp_path": self.ftp_path_var.get().strip(),
            "sw_local": self.sw_local_var.get().strip(),
            "downgrade_to_150": self.downgrade_var.get(),
            "backup": self.backup_var.get(),
        }
        CONFIG_FILE.write_text(json.dumps(cfg, ensure_ascii=False, indent=2), encoding="utf-8")
        if not silent:
            self.log("⚙️ Настройки сохранены.")

    def _load_config(self):
        if CONFIG_FILE.is_file():
            try:
                cfg = json.loads(CONFIG_FILE.read_text(encoding="utf-8"))
                self.cemu_path_var.set(cfg.get("cemu_path", ""))
                self.mode_var.set(cfg.get("mode", "ftp"))
                self.ftp_host_var.set(cfg.get("ftp_host", "192.168.1.100"))
                self.ftp_port_var.set(cfg.get("ftp_port", "5000"))
                self.ftp_path_var.set(
                    cfg.get("ftp_path", "/JKSV/The Legend of Zelda Breath of the Wild/Sync")
                )
                self.sw_local_var.set(cfg.get("sw_local", ""))
                self.downgrade_var.set(cfg.get("downgrade_to_150", True))
                self.backup_var.set(cfg.get("backup", True))
                self._toggle_mode()
                return
            except Exception:
                pass
        found = auto_discover_cemu_save()
        if found:
            self.cemu_path_var.set(found)


if __name__ == "__main__":
    app = BotwSyncApp()
    app.mainloop()
