#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ttptt: tim-to-png-to-tim by pav13
# python 3.4

MAIN_VERSION = "0.2-prototype"
MAIN_TITLE = "ttptt: tim-to-png-to-tim v{}".format(MAIN_VERSION)

import os
import sys
import json
import struct
import logging
import threading
import queue
import traceback
import array
from itertools import chain
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

try:
    from PIL import Image, ImageTk
except ImportError:
    raise SystemExit('Требуется Pillow: pip install Pillow')

try:
    from tkinterdnd2 import DND_FILES, TkinterDnD
    HAS_DND = True
except ImportError:
    HAS_DND = False

# ---------------------------------------------------------------- logging

_LOG_FORMAT = '[%(levelname)s] %(message)s'
_LOG_DATEFMT = '%H:%M:%S'


def _setup_logger():
    logger = logging.getLogger('ttptt')
    logger.setLevel(logging.DEBUG)
    logger.handlers.clear()
    logger.propagate = False
    formatter = logging.Formatter(_LOG_FORMAT, datefmt=_LOG_DATEFMT)
    stream = logging.StreamHandler()
    stream.setLevel(logging.INFO)
    stream.setFormatter(formatter)
    logger.addHandler(stream)
    return logger


log = _setup_logger()
log_formatter = logging.Formatter(_LOG_FORMAT, datefmt=_LOG_DATEFMT)


def _app_dir():
    if getattr(sys, 'frozen', False):
        return os.path.dirname(os.path.abspath(sys.executable))
    return os.path.dirname(os.path.abspath(__file__))


CONFIG_FILE = os.path.join(_app_dir(), 'ttptt-settings.json')
BPP_LABELS = ['0 (4-bit)', '1 (8-bit)', '2 (16-bit)', '3 (24-bit)']
_RECENT_LIMIT = 7
_DROP_IMAGE_EXTS = ('.png', '.bmp')
_DROP_PALETTE_EXTS = ('.pal', '.act', '.gpl')
_MAX_CLUT_ENTRIES = 65536

# ---------------------------------------------------------------- config

def load_config():
    try:
        with open(CONFIG_FILE, 'r', encoding='utf-8') as f:
            cfg = json.load(f)
            if isinstance(cfg, dict):
                return cfg
            log.warning('Файл настроек содержит не словарь — игнорируем')
            return {}
    except FileNotFoundError:
        return {}
    except Exception as e:
        log.warning('Не удалось прочитать настройки (%s): %s', CONFIG_FILE, e)
        return {}


def save_config(cfg):
    try:
        with open(CONFIG_FILE, 'w', encoding='utf-8') as f:
            json.dump(cfg, f, indent=2)
    except Exception as e:
        log.warning('Не удалось сохранить настройки (%s): %s', CONFIG_FILE, e)

# ---------------------------------------------------------------- colors / utils

def bgr555_to_rgb(c):
    r, g, b = c & 0x1F, (c >> 5) & 0x1F, (c >> 10) & 0x1F
    return ((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2))


def rgb_to_bgr555(r, g, b, stp=0):
    return (((stp & 1) << 15) | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3))

def _bgr555_array_to_rgb_list(vals):
    """array('H') -> list[(r,g,b)] (8-бит, как в bgr555_to_rgb)."""
    out = []
    append = out.append
    for c in vals:
        r = c & 0x1F
        g = (c >> 5) & 0x1F
        b = (c >> 10) & 0x1F
        append(((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2)))
    return out

def _bgr555_array_to_rgba_bytes(vals, out_len, alpha_stp=True):
    """array('H') -> bytes RGBA. STP=1 -> alpha=128 (если alpha_stp), иначе 255."""
    out = bytearray(out_len)
    o = 0
    for c in vals:
        r = c & 0x1F
        g = (c >> 5) & 0x1F
        b = (c >> 10) & 0x1F
        out[o] = (r << 3) | (r >> 2)
        out[o + 1] = (g << 3) | (g >> 2)
        out[o + 2] = (b << 3) | (b >> 2)
        if alpha_stp and (c & 0x8000):
            out[o + 3] = 128
        else:
            out[o + 3] = 255
        o += 4
    return bytes(out)

def make_checkerboard(w, h, size=8):
    w = max(1, int(w))
    h = max(1, int(h))
    size = max(1, int(size))
    tile_size = size * 2
    tile = Image.new('RGB', (tile_size, tile_size), (140, 140, 140))
    tpx = tile.load()
    for yy in range(tile_size):
        yy_odd = (yy // size) & 1
        for xx in range(tile_size):
            if ((xx // size) ^ yy_odd) == 0:
                tpx[xx, yy] = (190, 190, 190)
    out = Image.new('RGB', (w, h), (140, 140, 140))
    for y in range(0, h, tile_size):
        for x in range(0, w, tile_size):
            out.paste(tile, (x, y))
    return out

def parse_int_flexible(s):
    """Парсит целое: '0x..' → hex, '0b..' → bin, '0o..' → oct, иначе decimal."""
    s = s.strip()
    if not s:
        raise ValueError('Пустое значение')
    sign = 1
    body = s
    if body[0] in '+-':
        if body[0] == '-':
            sign = -1
        body = body[1:]
    low = body.lower()
    try:
        if low.startswith('0x'):
            return sign * int(body[2:], 16)
        if low.startswith('0b'):
            return sign * int(body[2:], 2)
        if low.startswith('0o'):
            return sign * int(body[2:], 8)
        return sign * int(body, 10)
    except (ValueError, IndexError):
        raise ValueError(
            'Не число: {!r} (ожидается decimal, 0xHEX, 0bBIN или 0oOCT)'.format(s))

# ---------------------------------------------------------------- palette I/O

def read_palette_png(path):
    img = Image.open(path).convert('RGB')
    w, h = img.size
    total = w * h
    if total > 256:
        log.warning(
            'Палитра из %s: %d×%d = %d пикселей. '
            'Будет использовано только первых 256 пикселей.',
            os.path.basename(path), w, h, total, total)
    px = img.load()
    return [px[x, y] for y in range(h) for x in range(w)], (w, h)


def write_palette_png(path, palette, w, h):
    img = Image.new('RGB', (w, h))
    px = img.load()
    for i, (r, g, b) in enumerate(palette[:w * h]):
        px[i % w, i // w] = (r, g, b)
    img.save(path)


def read_jasc_pal(path):
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        lines = f.read().splitlines()
    if not lines or not lines[0].strip().upper().startswith('JASC-PAL'):
        raise ValueError('Не JASC-PAL файл')
    count = int(lines[2].strip())
    palette = []
    for i in range(3, min(3 + count, len(lines))):
        parts = lines[i].split()
        if len(parts) >= 3:
            palette.append((int(parts[0]), int(parts[1]), int(parts[2])))
    return palette


def write_jasc_pal(path, palette):
    with open(path, 'w', encoding='utf-8') as f:
        f.write('JASC-PAL\n0100\n{}\n'.format(len(palette)))
        for r, g, b in palette:
            f.write('{} {} {}\n'.format(r, g, b))


def read_act(path):
    with open(path, 'rb') as f:
        data = f.read()
    if len(data) == 772:
        raw = struct.unpack('>H', data[768:770])[0]
        n = 256 if raw == 0 else min(raw, 256)
    elif len(data) == 768:
        n = 256
    elif len(data) >= 3 and len(data) % 3 == 0 and len(data) // 3 <= 256:
        n = len(data) // 3
    else:
        raise ValueError('Неверный .act ({} байт)'.format(len(data)))
    return [(data[i * 3], data[i * 3 + 1], data[i * 3 + 2]) for i in range(n)]


def write_act(path, palette):
    out = bytearray()
    for i in range(256):
        r, g, b = palette[i] if i < len(palette) else (0, 0, 0)
        out.extend([r, g, b])
    count = len(palette) if len(palette) < 256 else 0
    out.extend(struct.pack('>H', count))
    out.extend(struct.pack('>H', 0xFFFF))
    with open(path, 'wb') as f:
        f.write(out)


def read_gpl(path):
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        lines = f.read().splitlines()
    if not lines or not lines[0].strip().upper().startswith('GIMP PALETTE'):
        raise ValueError('Не GPL файл')
    palette = []
    for line in lines[1:]:
        s = line.strip()
        if not s or s.startswith('#') or s.startswith('Name:') or s.startswith('Columns:'):
            continue
        parts = s.split()
        if len(parts) >= 3:
            try:
                palette.append((int(parts[0]), int(parts[1]), int(parts[2])))
            except ValueError:
                continue
    return palette


def write_gpl(path, palette):
    with open(path, 'w', encoding='utf-8') as f:
        f.write('GIMP Palette\nName: ttptt export\nColumns: 16\n#\n')
        for r, g, b in palette:
            f.write('{:3d} {:3d} {:3d}\tUntitled\n'.format(r, g, b))


def load_palette_any(path):
    ext = os.path.splitext(path)[1].lower()
    if ext in ('.png', '.bmp'): return read_palette_png(path)
    if ext == '.pal': return read_jasc_pal(path), None
    if ext == '.act': return read_act(path), None
    if ext == '.gpl': return read_gpl(path), None
    raise ValueError('Неподдерживаемый формат: {}'.format(ext))


def save_palette_any(path, palette, w, h):
    ext = os.path.splitext(path)[1].lower()
    if ext in ('.png', '.bmp'): write_palette_png(path, palette, w, h)
    elif ext == '.pal': write_jasc_pal(path, palette)
    elif ext == '.act': write_act(path, palette)
    elif ext == '.gpl': write_gpl(path, palette)
    else: raise ValueError('Неподдерживаемый формат: {}'.format(ext))

def safe_clut_flat(group, max_entries=_MAX_CLUT_ENTRIES):
    """Возвращает (flat_palette, cw, ch), обрезая раздувание от битых заголовков."""
    if not (group.get('clut') and group.get('clut_header')):
        return [], 0, 0
    cw = group['clut_header']['width_words']
    ch = group['clut_header']['height'] or 1
    total = cw * ch
    if total > max_entries:
        log.warning('CLUT: cw*ch=%d превышает лимит %d — обрезаю', total, max_entries)
        if cw > max_entries:
            cw = max_entries
            ch = 1
        else:
            ch = max(1, max_entries // cw)
        total = cw * ch

    need = total * 2
    data = group['clut']
    if len(data) < need:
        data = data + b'\x00' * (need - len(data))
    elif len(data) > need:
        data = data[:need]

    vals = array.array('H')
    vals.frombytes(data)
    return _bgr555_array_to_rgb_list(vals), cw, ch

# ---------------------------------------------------------------- TIM core

def image_width_words_to_pixels(bpp, words):
    if bpp == 0: return words * 4
    if bpp == 1: return words * 2
    if bpp == 2: return words
    if bpp == 3: return (words * 2) // 3
    return words


def pixels_to_words(bpp, pixels):
    if bpp == 0: return (pixels + 3) // 4
    if bpp == 1: return (pixels + 1) // 2
    if bpp == 2: return pixels
    if bpp == 3: return (pixels * 3 + 1) // 2
    return pixels


def parse_tims(data):
    if not data:
        raise ValueError('Пустой файл')

    MAX_GROUPS = 100
    MAX_ITERATIONS = 2000
    MAX_GARBAGE_LOGGED = 3

    idx = data.find(b'\x10\x00\x00\x00')
    if idx == -1:
        raise ValueError('Сигнатура TIM (0x10) не найдена в файле')
    if idx > 0:
        log.info('Пропущено %d байт до первой сигнатуры TIM', idx)
    offset = idx

    groups = []
    garbage_skips = 0
    iterations = 0

    while offset < len(data) and len(groups) < MAX_GROUPS:
        iterations += 1
        if iterations > MAX_ITERATIONS:
            log.warning('parse_tims: лимит итераций (%d) исчерпан', MAX_ITERATIONS)
            break

        if not data.startswith(b'\x10\x00\x00\x00', offset):
            next_sig = data.find(b'\x10\x00\x00\x00', offset)
            if next_sig == -1:
                break
            garbage_skips += 1
            if garbage_skips <= MAX_GARBAGE_LOGGED:
                log.warning('Пропуск мусора между TIM-группами: %d байт', next_sig - offset)
            elif garbage_skips == MAX_GARBAGE_LOGGED + 1:
                log.warning('...дальнейшие пропуски мусора не логируются')
            offset = next_sig
            continue

        try:
            group, offset = parse_tim_group(data, offset)
            groups.append(group)
            garbage_skips = 0
        except Exception as e:
            if groups:
                groups[-1]['warnings'].append(
                    'Критическая ошибка парсинга @{}: {}'.format(offset, e))
            log.warning('Ошибка парсинга TIM @%d: %s', offset, e)
            next_sig = data.find(b'\x10\x00\x00\x00', offset + 4)
            if next_sig == -1:
                break
            offset = next_sig

    if len(groups) >= MAX_GROUPS:
        log.warning('Достигнут лимит групп (%d) — парсинг остановлен', MAX_GROUPS)

    if not groups:
        raise ValueError('Не удалось распарсить ни одной TIM-группы')
    return groups


def parse_tim_group(data, offset=0, check_signature=True):
    start = offset
    if offset + 8 > len(data):
        raise ValueError('Обрезан заголовок TIM')

    if check_signature:
        if not data.startswith(b'\x10\x00\x00\x00', offset):
            raise ValueError('Неверная сигнатура TIM')
        flags = struct.unpack('<I', data[offset + 4:offset + 8])[0]
        offset += 8
    else:
        if data.startswith(b'\x10\x00\x00\x00', offset):
            flags = struct.unpack('<I', data[offset + 4:offset + 8])[0]
            offset += 8
        else:
            flags = struct.unpack('<I', data[offset:offset + 4])[0]
            offset += 4
            bpp = flags & 0x7
            if bpp > 3:
                raise ValueError('Недопустимое значение bpp ({}) - файл не похож на TIM'.format(bpp))

    bpp = flags & 0x7
    has_clut = bool(flags & 0x8)
    has_image_flag = bool(flags & 0x10)

    group = {
        'flags': flags, 'bpp': bpp, 'has_clut': has_clut, 'has_image': has_image_flag,
        'clut_header': None, 'clut': None, 'images': [], 'extra': b'', 'warnings': [],
        'start_offset': start,
    }

    MAX_WARNINGS_PER_GROUP = 13
    warn_count = [0]

    def _warn(msg):
        if warn_count[0] < MAX_WARNINGS_PER_GROUP:
            group['warnings'].append(msg)
        elif warn_count[0] == MAX_WARNINGS_PER_GROUP:
            group['warnings'].append('... и ещё предупреждения')
        warn_count[0] += 1

    # ----- CLUT -----
    if has_clut:
        if offset + 12 > len(data):
            _warn('Обрезан заголовок CLUT, данные потеряны')
            group['clut_header'] = {'size': 12, 'x': 0, 'y': 0, 'width_words': 16, 'height': 1}
            group['clut'] = b''
            offset = len(data)
        else:
            size, cx, cy, cw, ch = struct.unpack('<IHHHH', data[offset:offset + 12])
            if size < 12 or size > 1024 * 1024:
                _warn('Неверный размер CLUT ({}), исправлено на 12'.format(size))
                size = 12
                cw = min(cw, 256)
                ch = min(ch, 16)
            offset += 12

            declared_data = max(0, size - 12)
            if cw * ch * 2 > declared_data and declared_data >= 0:
                max_entries = max(1, declared_data // 2)
                if cw > max_entries:
                    cw = min(cw, max_entries)
                    ch = 1
                else:
                    ch = max(1, min(ch, max_entries // cw))
                _warn('CLUT: cw*ch не соответствует size, обрезано до cw={}, ch={}'.format(cw, ch))

            need = size - 12
            avail = len(data) - offset
            if avail < need:
                _warn('CLUT обрезан: есть {} байт, заявлено {}'.format(avail, need))
                group['clut_header'] = {'size': size, 'x': cx, 'y': cy,
                                        'width_words': cw, 'height': ch}
                group['clut'] = data[offset:]
                offset = len(data)
            else:
                group['clut_header'] = {'size': size, 'x': cx, 'y': cy,
                                        'width_words': cw, 'height': ch}
                group['clut'] = data[offset:offset + need]
                offset += need

    # ----- эвристика: подозрительно маленький CLUT при bpp=1 (8-bit) -----
    if bpp == 1 and group.get('clut_header'):
        _cw = group['clut_header']['width_words']
        _ch = group['clut_header']['height'] or 1
        if _cw < 128 and _cw % 16 == 0 and _cw * _ch <= 384:
            _warn('bpp=1 (8-bit) по флагам, но CLUT {}×{} слов '
                  '(<128 слов в ширину, кратно 16) — похоже на 4-bit TIM '
                  'с набором 16-цветных палитр. Возможно, файл записан с '
                  'неверным bpp; переключите bpp на 4-bit в панели '
                  '«Параметры TIM».'.format(_cw, _ch))
    
    # ----- IMAGE -----
    max_images = 100
    MAX_SCAN_ITERS = 200
    images_parsed = 0
    scan_iters = 0

    while (offset + 12 <= len(data)
           and images_parsed < max_images
           and scan_iters < MAX_SCAN_ITERS):
        scan_iters += 1

        if data.startswith(b'\x10\x00\x00\x00', offset):
            break

        size, ix, iy, iw, ih = struct.unpack('<IHHHH', data[offset:offset + 12])

        if size < 12 or size > 100 * 1024 * 1024:
            _warn('Неверный размер блока изображения ({}), пропуск'.format(size))
            offset += 12
            continue

        if (iw == 0 or ih == 0
                or iw > 2048 or ih > 2048
                or iw * 2 * ih > 4 * 1024 * 1024):
            _warn('Нереалистичные размеры изображения ({}×{} слов, ~{} МБ данных), пропуск блока'.format(
                iw, ih, (iw * 2 * ih) // (1024 * 1024)))
            remaining = len(data) - offset
            offset += size if 12 <= size <= remaining else 12
            continue

        expected = iw * 2 * ih
        declared = size - 12

        if declared > 50 * 1024 * 1024:
            _warn('Слишком большой размер данных ({}), пропуск'.format(declared))
            remaining = len(data) - offset
            offset += size if 12 <= size <= remaining else 12
            continue

        if abs(declared - expected) > 4:
            _warn('Несоответствие размера: заявлено {}, расчетное {}'.format(declared, expected))

        avail = len(data) - offset - 12
        data_len = min(declared, avail)

        if data_len <= 0:
            _warn('Нет данных для изображения, конец парсинга')
            break

        image_data = data[offset + 12:offset + 12 + data_len]
        group['images'].append({
            'header': {'size': size, 'x': ix, 'y': iy, 'width_words': iw, 'height': ih},
            'data': image_data,
        })

        advance = max(size, 12 + data_len)
        offset += advance
        images_parsed += 1

        if data_len < declared:
            _warn('Данные изображения обрезаны (не хватает {} байт)'.format(declared - data_len))
            break

    if scan_iters >= MAX_SCAN_ITERS and images_parsed < max_images:
        _warn('Превышен лимит сканирования ({} итераций) — дальнейшие блоки не читаются'.format(
            MAX_SCAN_ITERS))

    if not group['images'] and has_image_flag:
        _warn('has_image=1, но image-блок не найден или битый')
    if group['images'] and not has_image_flag:
        _warn('Изображение есть, но has_image=0')

    if offset < len(data) and data[offset:offset + 4] != b'\x10\x00\x00\x00':
        group['extra'] = data[offset:]
        offset = len(data)

    return group, offset


def rebuild_tim_group(group):
    out = bytearray()
    out.extend(struct.pack('<I', 0x10))
    out.extend(struct.pack('<I', group['flags']))

    if group.get('has_clut') and group.get('clut') is not None:
        h = group['clut_header']
        clut = group['clut']
        size_field = max(12, h['size'])
        declared = size_field - 12
        if len(clut) < declared:
            clut = clut.ljust(declared, b'\x00')
        elif len(clut) > declared:
            clut = clut[:declared]
        out.extend(struct.pack('<IHHHH', size_field, h['x'], h['y'],
                               h['width_words'], h['height']))
        out.extend(clut)

    for img in group.get('images', []):
        h = img['header']
        data = img['data']
        size_field = max(12, h['size'])
        declared = size_field - 12
        if len(data) < declared:
            data = data.ljust(declared, b'\x00')
        elif len(data) > declared:
            data = data[:declared]
        out.extend(struct.pack('<IHHHH', size_field, h['x'], h['y'],
                               h['width_words'], h['height']))
        out.extend(data)

    out.extend(group.get('extra', b''))
    return bytes(out)


def rebuild_all(groups):
    return b''.join(rebuild_tim_group(g) for g in groups)


def make_entries(groups):
    entries = []
    for gi, g in enumerate(groups):
        if g['images']:
            for ii in range(len(g['images'])):
                entries.append((gi, ii))
        else:
            entries.append((gi, -1))
    return entries

def get_clut_palette(group, palette_row=0):
    if not (group.get('clut') and group.get('clut_header')):
        return []
    cw = group['clut_header']['width_words']
    ch = group['clut_header']['height'] or 1
    palette_row = min(palette_row, ch - 1)

    need = cw * ch * 2
    data = group['clut']
    if len(data) < need:
        data = data + b'\x00' * (need - len(data))
    elif len(data) > need:
        data = data[:need]

    vals = array.array('H')
    vals.frombytes(data)

    start = palette_row * cw
    return _bgr555_array_to_rgb_list(vals[start:start + cw])

def get_clut_palette_rgba(group, palette_row=0):
    """Как get_clut_palette, но с alpha: STP=1 -> 128, иначе 255."""
    if not (group.get('clut') and group.get('clut_header')):
        return []
    cw = group['clut_header']['width_words']
    ch = group['clut_header']['height'] or 1
    palette_row = min(palette_row, ch - 1)

    need = cw * ch * 2
    data = group['clut']
    if len(data) < need:
        data = data + b'\x00' * (need - len(data))
    elif len(data) > need:
        data = data[:need]

    vals = array.array('H')
    vals.frombytes(data)

    start = palette_row * cw
    out = []
    for c in vals[start:start + cw]:
        r = c & 0x1F
        g = (c >> 5) & 0x1F
        b = (c >> 10) & 0x1F
        a = 128 if (c & 0x8000) else 255
        out.append(((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2), a))
    return out

def render_palette_image(group, palette_row=0):
    palette = get_clut_palette(group, palette_row)
    if not palette:
        return Image.new('RGB', (64, 64), (32, 32, 32))
    cell_w = 4
    img = Image.new('RGB', (len(palette) * cell_w, 32), (0, 0, 0))
    px = img.load()
    for i, (r, g, b) in enumerate(palette):
        for yy in range(32):
            for xx in range(cell_w):
                px[i * cell_w + xx, yy] = (r, g, b)
    return img

def decode_image(group, image_idx, palette_row=0, alpha_stp=None):
    """alpha_stp=None -> авто (P-mode для 4/8bpp, RGBA с STP для 16bpp);
    True -> везде RGBA с STP-альфой; False -> везде непрозрачно."""
    if image_idx < 0 or image_idx >= len(group['images']):
        return render_palette_image(group, palette_row)

    image = group['images'][image_idx]
    hdr = image['header']
    bpp = group['bpp']
    width = image_width_words_to_pixels(bpp, hdr['width_words'])
    height = hdr['height']
    data = image['data']

    if width <= 0 or height <= 0:
        return Image.new('RGB', (max(1, width), max(1, height)), (0, 0, 0))

    # ---------------- 4bpp / 8bpp ----------------
    if bpp in (0, 1):
        palette = get_clut_palette(group, palette_row)
        n_px = width * height
        pal_size = 16 if bpp == 0 else 256

        # ---- индексы ----
        if bpp == 0:
            n_bytes = (n_px + 1) // 2
            raw = data[:n_bytes]
            if len(raw) < n_bytes:
                raw = raw + b'\x00' * (n_bytes - len(raw))
            idx = bytearray(n_px)
            n_pairs = n_px // 2
            for i in range(n_pairs):
                b = raw[i]
                idx[i * 2] = b & 0x0F
                idx[i * 2 + 1] = (b >> 4) & 0x0F
            if n_px & 1:
                idx[-1] = raw[n_pairs] & 0x0F
            indices = bytes(idx)
        else:
            raw = data[:n_px]
            if len(raw) < n_px:
                raw = raw + b'\x00' * (n_px - len(raw))
            indices = raw

        # ---- нет палитры -> градации серого ----
        if not palette:
            if bpp == 0:
                gray_table = bytes((v << 4) | v for v in range(16))
                gray_bytes = bytes(gray_table[v] for v in indices)
            else:
                gray_bytes = indices
            gray = Image.frombytes('L', (width, height), gray_bytes).convert('RGB')
            if alpha_stp is True:
                return gray.convert('RGBA')
            return gray

        # ---- alpha_stp явно включён -> RGBA с STP из палитры ----
        if alpha_stp is True:
            palette_rgba = get_clut_palette_rgba(group, palette_row)
            lut = bytearray(256 * 4)
            for i in range(256):
                lut[i * 4:i * 4 + 4] = b'\x00\x00\x00\xff'
            for i in range(min(pal_size, len(palette_rgba))):
                lut[i * 4:i * 4 + 4] = bytes(palette_rgba[i])
            lut = bytes(lut)

            r = indices.translate(lut[0::4])
            g = indices.translate(lut[1::4])
            b = indices.translate(lut[2::4])
            a = indices.translate(lut[3::4])
            out = bytearray(n_px * 4)
            out[0::4] = r
            out[1::4] = g
            out[2::4] = b
            out[3::4] = a
            return Image.frombytes('RGBA', (width, height), bytes(out))

        # ---- P-mode (как раньше, для сохранения в 4/8-bit PNG) ----
        flat_pal = []
        for i in range(pal_size):
            if i < len(palette):
                flat_pal.extend(palette[i])
            else:
                flat_pal.extend((0, 0, 0))
        img = Image.frombytes('P', (width, height), indices)
        img.putpalette(flat_pal)
        return img

    # ---------------- 16bpp ----------------
    if bpp == 2:
        n_px = width * height
        need = n_px * 2
        raw = data[:need]
        if len(raw) < need:
            raw = raw + b'\x00' * (need - len(raw))
        vals = array.array('H')
        vals.frombytes(raw)
        # None -> ведём себя как раньше (STP учитывается)
        use_stp = (alpha_stp is not False)
        rgba = _bgr555_array_to_rgba_bytes(vals, n_px * 4, alpha_stp=use_stp)
        return Image.frombytes('RGBA', (width, height), rgba)

    # ---------------- 24bpp ----------------
    if bpp == 3:
        stride = hdr['width_words'] * 2
        row_bytes = width * 3
        rows = []
        for y in range(height):
            start = y * stride
            end = start + row_bytes
            if end <= len(data):
                rows.append(data[start:end])
            else:
                piece = data[start:min(end, len(data))]
                rows.append(piece + b'\x00' * (row_bytes - len(piece)))
        raw_rgb = b''.join(rows)
        return Image.frombytes('RGB', (width, height), raw_rgb).convert('RGBA')

    return Image.new('RGB', (max(1, width), max(1, height)), (0, 0, 0))
    
def encode_image(img, group, image_header, palette_row=0):
    bpp = group['bpp']
    width = image_width_words_to_pixels(bpp, image_header['width_words'])
    height = image_header['height']
    if width <= 0 or height <= 0:
        raise ValueError('Нулевые размеры изображения')

    if img.size != (width, height):
        img = img.resize((width, height), Image.NEAREST)

    # ---------------- 4bpp / 8bpp ----------------
    if bpp in (0, 1):
        palette = get_clut_palette(group, palette_row)
        if not palette:
            raise ValueError('Палитра пуста')
        max_idx = (1 << (4 if bpp == 0 else 8)) - 1
        n_px = width * height

        if img.mode == 'P':
            src_indices = list(img.getdata())
            src_pal = img.getpalette() or []
            src_pal_rgb = [tuple(src_pal[i:i + 3]) for i in range(0, len(src_pal) - 2, 3)]

            cache = {}
            remap = {}
            for idx in set(src_indices):
                if (idx <= max_idx and idx < len(palette)
                        and idx < len(src_pal_rgb)
                        and palette[idx] == src_pal_rgb[idx]):
                    remap[idx] = idx
                    continue
                color = src_pal_rgb[idx] if idx < len(src_pal_rgb) else (0, 0, 0)
                if color in cache:
                    remap[idx] = cache[color]
                else:
                    best_i, best_d = 0, 1 << 30
                    cr, cg, cb = color
                    for j, (pr, pg, pb) in enumerate(palette):
                        d = (pr - cr) ** 2 + (pg - cg) ** 2 + (pb - cb) ** 2
                        if d < best_d:
                            best_d, best_i = d, j
                    cache[color] = best_i
                    remap[idx] = best_i

            indices = bytearray(n_px)
            for i in range(n_px):
                indices[i] = remap[src_indices[i]] if i < len(src_indices) else 0
        else:
            img_rgba = img.convert('RGBA')
            raw = img_rgba.tobytes()
            n = width * height
            indices = bytearray(n)
            color_map = {}
            for i, col in enumerate(palette):
                if col not in color_map:
                    color_map[col] = i

            cache = {}
            for i in range(n):
                r = raw[i * 4]
                g = raw[i * 4 + 1]
                b = raw[i * 4 + 2]
                a = raw[i * 4 + 3]
                if a < 255:
                    indices[i] = 0
                    continue
                key = (r, g, b)
                v = cache.get(key)
                if v is not None:
                    indices[i] = v
                    continue
                idx = color_map.get(key)
                if idx is None:
                    best_i, best_d = 0, 1 << 30
                    for j, (pr, pg, pb) in enumerate(palette):
                        d = (pr - r) ** 2 + (pg - g) ** 2 + (pb - b) ** 2
                        if d < best_d:
                            best_d, best_i = d, j
                    idx = best_i
                if idx > max_idx:
                    idx = 0
                cache[key] = idx
                indices[i] = idx

        if bpp == 0:
            out = bytearray((n_px + 1) // 2)
            pairs = n_px // 2
            for i in range(pairs):
                out[i] = ((indices[i * 2 + 1] & 0x0F) << 4) | (indices[i * 2] & 0x0F)
            if n_px & 1:
                out[pairs] = indices[-1] & 0x0F
            return bytes(out)
        return bytes(indices)

    # ---------------- 16bpp ----------------
    if bpp == 2:
        img_rgba = img.convert('RGBA')
        raw = img_rgba.tobytes()
        n = width * height
        out = array.array('H', [0]) * n
        for i in range(n):
            r = raw[i * 4]
            g = raw[i * 4 + 1]
            b = raw[i * 4 + 2]
            a = raw[i * 4 + 3]
            stp = 1 if a < 255 else 0
            out[i] = ((stp & 1) << 15) | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)
        return out.tobytes()

    # ---------------- 24bpp ----------------
    if bpp == 3:
        stride = image_header['width_words'] * 2
        img_rgb = img.convert('RGB')
        raw = img_rgb.tobytes()
        row_bytes = width * 3
        out = bytearray(stride * height)
        for y in range(height):
            src = y * row_bytes
            dst = y * stride
            out[dst:dst + row_bytes] = raw[src:src + row_bytes]
        return bytes(out)

    raise ValueError('Неподдерживаемый bpp')

# ---------------------------------------------------------------- log window

class _LogWindowHandler(logging.Handler):
    """Потокобезопасный handler: emit() кладёт строку в queue, drain() трогает tk.Text
    только из главного потока (вызывается через window.after)."""

    MAX_QUEUE = 5000

    def __init__(self, text_widget, root):
        super().__init__()
        self.text = text_widget
        self.root = root
        self.queue = queue.Queue(maxsize=self.MAX_QUEUE)
        self._drop_count = 0
        self._stop = False

    def emit(self, record):
        if self._stop or self.text is None:
            return
        try:
            msg = self.format(record)
        except Exception:
            return
        try:
            self.queue.put_nowait(msg)
        except queue.Full:
            self._drop_count += 1

    def drain(self):
        if self._stop or self.text is None:
            return
        try:
            if not self.text.winfo_exists():
                return
        except Exception:
            return
        msgs = []
        try:
            while True:
                msgs.append(self.queue.get_nowait())
        except queue.Empty:
            pass
        if self._drop_count:
            msgs.append('...пропущено {} сообщений (очередь переполнена)'.format(self._drop_count))
            self._drop_count = 0
        if not msgs:
            return
        try:
            self.text.configure(state='normal')
            self.text.insert(tk.END, '\n'.join(msgs) + '\n')
            self.text.see(tk.END)
            self.text.configure(state='disabled')
        except Exception:
            pass


class LogWindow:
    """Неблокирующее дочернее окно с журналом логов программы."""

    def __init__(self, app, logger, var):
        if hasattr(app, 'root') and hasattr(app, '_add_themed_apply'):
            self.app = app
            self.root = app.root
        else:
            self.app = None
            self.root = app
        self.logger = logger
        self.var = var
        self.window = None
        self.text = None
        self.handler = None
        self._remove_themed = None

    def toggle(self):
        if self.var.get():
            self.open()
        else:
            self.close()

    def _apply_colors(self, colors):
        if self.window is None or self.text is None:
            return
        try:
            if not self.window.winfo_exists():
                return
        except Exception:
            return
        try:
            self.window.configure(bg=colors['bg'])
        except Exception:
            pass
        try:
            self.text.configure(
                bg=colors['bg2'], fg=colors['fg'],
                insertbackground=colors['fg'],
                selectbackground=colors['sel'],
                selectforeground='#ffffff',
            )
        except Exception:
            pass

    def open(self):
        if self.window is not None:
            try:
                self.window.lift()
            except Exception:
                pass
            self.var.set(True)
            return

        colors = self.app._theme_colors()

        self.window = tk.Toplevel(self.root)
        self.window.title('Журнал логов')
        self.window.geometry('640x480')
        self.window.minsize(100, 100)
        self.window.protocol('WM_DELETE_WINDOW', self.close)
        try:
            self.window.configure(bg=colors['bg'])
        except Exception:
            pass

        outer = ttk.Frame(self.window, padding=4)
        outer.pack(fill=tk.BOTH, expand=True)

        text_frame = ttk.Frame(outer)
        text_frame.pack(fill=tk.BOTH, expand=True)

        font = ('Consolas', 9) if sys.platform == 'win32' else ('Monospace', 10)
        self.text = tk.Text(
            text_frame, wrap=tk.WORD, state='disabled', font=font,
            bg=colors['bg2'], fg=colors['fg'],
            insertbackground=colors['fg'],
            selectbackground=colors['sel'], selectforeground='#ffffff',
            relief='flat', bd=1, highlightthickness=0,
        )
        sb = ttk.Scrollbar(text_frame, orient='vertical', command=self.text.yview)
        self.text.configure(yscrollcommand=sb.set)
        self.text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        sb.pack(side=tk.RIGHT, fill=tk.Y)

        btn_frame = ttk.Frame(outer)
        btn_frame.pack(fill=tk.X, pady=(4, 0))
        ttk.Button(btn_frame, text='Очистить журнал', command=self.clear).pack(side=tk.LEFT, padx=(0, 4))
        ttk.Button(btn_frame, text='Копировать', command=self.copy).pack(side=tk.LEFT, padx=(0, 4))
        ttk.Button(btn_frame, text='Закрыть журнал', command=self.close).pack(side=tk.RIGHT)

        self.handler = _LogWindowHandler(self.text, self.root)
        self.handler.setFormatter(log_formatter)
        self.logger.addHandler(self.handler)

        def _drain_loop():
            if self.handler is None or self.window is None:
                return
            self.handler.drain()
            try:
                if self.window.winfo_exists():
                    self.window.after(120, _drain_loop)
            except Exception:
                pass

        self.window.after(120, _drain_loop)

        self._remove_themed = self.app._add_themed_apply(self.window, self._apply_colors)
        self.var.set(True)

    def close(self):
        if self.handler is not None:
            try:
                self.handler.drain()
            except Exception:
                pass
            self.handler._stop = True
            self.logger.removeHandler(self.handler)
            self.handler = None
        if self._remove_themed is not None:
            try:
                self._remove_themed()
            except Exception:
                pass
            self._remove_themed = None
        if self.window is not None:
            self.text = None
            try:
                self.window.destroy()
            except Exception:
                pass
            self.window = None
        self.var.set(False)

    def clear(self):
        if self.text is not None:
            try:
                self.text.configure(state='normal')
                self.text.delete('1.0', tk.END)
                self.text.configure(state='disabled')
            except Exception:
                pass

    def copy(self):
        if self.text is not None:
            try:
                content = self.text.get('1.0', tk.END)
                self.root.clipboard_clear()
                self.root.clipboard_append(content)
            except Exception:
                pass

# ---------------------------------------------------------------- TTPTT GUI

class TTPTT:
    # ============ Инициализация ============

    def __init__(self, root):
        self.root = root
        self.root.title(MAIN_TITLE)
        self._set_window_icon()

        self.groups, self.entries = [], []
        self.current_entry = -1
        self.source_stem = self.source_path = None
        self.palette_row = 0
        self.preview_image = self.preview_photo = None
        self._palette_cells = []
        self._pal_photo = None
        self._render_after_id = None
        self.all_menus = []
        self.theme_mode = 'light'
        self.dirty = False
        self._batch_running = False
        self.config = load_config()

        self.recent_files = self.config.get('recent_files', [])
        if not isinstance(self.recent_files, list):
            self.recent_files = []
        self.recent_files = [p for p in self.recent_files if isinstance(p, str)][:_RECENT_LIMIT]
        self.config['recent_files'] = list(self.recent_files)
        zf = self.config.get('zoom_fit', True)
        self.zoom_fit = bool(zf) if isinstance(zf, bool) else True
        self.show_transparency = tk.BooleanVar(
            value=bool(self.config.get('show_transparency', False)))
        self.auto_save_pal = {
            'act': tk.BooleanVar(value=self.config.get('auto_save_act_with_png', True)),
            'pal': tk.BooleanVar(value=self.config.get('auto_save_pal_with_png', False)),
            'gpl': tk.BooleanVar(value=self.config.get('auto_save_gpl_with_png', False)),
        }
        for fmt in self.auto_save_pal:
            self.auto_save_pal[fmt].trace(
                'w', lambda n, i, m, f=fmt: self._on_auto_save_change(f))

        self.show_log_var = tk.BooleanVar(value=bool(self.config.get('show_log', False)))
        self.log_window = LogWindow(self, log, self.show_log_var)
        self._themed_windows = []

        self._build_menu()
        self._build_ui()
        self.apply_theme(self.config.get('theme', 'light'))
        self._setup_bindings()
        self._setup_drag_and_drop()
        self.root.protocol('WM_DELETE_WINDOW', self._on_close_window)
        self._restore_window_state()
        self._update_title()

        if self.show_log_var.get():
            try:
                self.log_window.open()
            except Exception as e:
                log.warning('Не удалось открыть журнал: %s', e)

        log.info('Приложение готово, недавних файлов: %d, Drag n Drop: %s',
                 len(self.recent_files), 'да' if HAS_DND else 'нет')

    # ============ Setup: bindings / DnD / icon / window ============

    def _setup_drag_and_drop(self):
        if HAS_DND:
            self.root.drop_target_register(DND_FILES)
            self.root.dnd_bind('<<Drop>>', self._on_file_drop)
        else:
            log.warning('tkinterdnd2 недоступен — Drag & Drop отключён')

    def _setup_bindings(self):
        def bind_safe(seq, handler):
            def cb(event):
                try:
                    handler()
                except Exception as e:
                    log.exception('Ошибка обработчика %s: %s', seq, e)
                return 'break'
            self.root.bind(seq, cb)

        bind_safe('<Control-Return>', self.apply_metadata)
        bind_safe('<Control-Left>', lambda: self.switch_image(-1))
        bind_safe('<Control-Right>', lambda: self.switch_image(+1))
        bind_safe('<Control-Up>', lambda: self.palette_row_step(+1))
        bind_safe('<Control-Down>', lambda: self.palette_row_step(-1))
        
        if sys.platform == 'win32':
            VK_O, VK_R, VK_S, VK_W = 79, 82, 83, 87
            VK_1, VK_2 = 49, 50
            SHIFT_BIT = 0x1

            plain_map = {
                VK_O: self.open_tim,
                VK_R: self.replace_image,
                VK_S: lambda: self.save_tim(save_as=False),
                VK_W: self.close_file,
                VK_1: lambda: self.preview_scale(False),
                VK_2: lambda: self.preview_scale(True),
            }
            shift_map = {
                VK_O: self.open_raw_tim,
                VK_R: self.reset_metadata,
                VK_S: self.save_png,
            }

            def on_ctrl_keypress(event):
                code = event.keycode
                shift = bool(event.state & SHIFT_BIT)
                action = (shift_map if shift else plain_map).get(code)
                if action is not None:
                    try:
                        action()
                    except Exception as e:
                        log.exception('Ошибка обработчика горячей клавиши: %s', e)
                    return 'break'

            self.root.bind('<Control-KeyPress>', on_ctrl_keypress)
        else:
            bind_safe('<Control-1>', lambda: self.preview_scale(False))
            bind_safe('<Control-2>', lambda: self.preview_scale(True))
            bind_safe('<Control-o>', self.open_tim)
            bind_safe('<Control-Shift-O>', self.open_raw_tim)
            bind_safe('<Control-r>', self.replace_image)
            bind_safe('<Control-Shift-R>', self.reset_metadata)
            bind_safe('<Control-s>', lambda: self.save_tim(save_as=False))
            bind_safe('<Control-Shift-S>', self.save_png)
            bind_safe('<Control-w>', self.close_file)

    def _resource_path(self, name):
        base = getattr(sys, '_MEIPASS', None)
        if base:
            p = Path(base) / name
            if p.is_file():
                return p
        return Path(_app_dir()) / name

    def _set_window_icon(self):
        ico = self._resource_path('icon.ico')
        if ico.is_file() and sys.platform == 'win32':
            try:
                self.root.iconbitmap(default=str(ico))
            except Exception as e:
                log.warning('Не удалось установить иконку окна: %s', e)

    def _restore_window_state(self):
        geom = self.config.get('window_geometry')
        if isinstance(geom, str) and geom:
            try:
                self.root.geometry(geom)
            except Exception as e:
                log.warning('Не удалось восстановить геометрию окна: %s', e)
                self.root.geometry('840x500+15+15')
        else:
            self.root.geometry('840x500')

        if self.config.get('window_state') == 'zoomed':
            try:
                self.root.state('zoomed')
            except Exception:
                try:
                    self.root.attributes('-zoomed', True)
                except Exception:
                    pass

    # ============ Menu ============

    def _build_menu(self):
        mb = tk.Menu(self.root)
        self.menubar = mb

        fm = tk.Menu(mb, tearoff=0)
        self._file_menu = fm
        self._populate_file_menu()
        mb.add_cascade(label='Файл', menu=fm)
        self.all_menus.append(fm)

        pm = tk.Menu(mb, tearoff=0)
        pm.add_command(label='Экспорт палитры ...', command=self.export_palette)
        pm.add_command(label='Импорт палитры ...', command=self.import_palette)
        pm.add_separator()
        pm.add_checkbutton(label='Сохранять Adobe Color Table (ACT) вместе с PNG',
                           variable=self.auto_save_pal['act'])
        pm.add_checkbutton(label='Сохранять JASC (PAL) вместе с PNG',
                           variable=self.auto_save_pal['pal'])
        pm.add_checkbutton(label='Сохранять GIMP (GPL) вместе с PNG',
                           variable=self.auto_save_pal['gpl'])
        mb.add_cascade(label='Палитра', menu=pm)
        self.all_menus.append(pm)

        cm = tk.Menu(mb, tearoff=0)
        cm.add_command(label='Пакетное извлечение изображений из TIM',
                       command=self.batch_extract)
        cm.add_command(label='Пакетная замена изображений в TIM',
                       command=self.batch_replace)
        mb.add_cascade(label='Пакетная обработка', menu=cm)
        self.all_menus.append(cm)

        em = tk.Menu(mb, tearoff=0)
        em.add_command(label='Сбросить изменения', accelerator='Ctrl+Shift+R',
                       command=self.reset_metadata)
        em.add_command(label='Применить метаданные', accelerator='Ctrl+Enter',
                       command=self.apply_metadata)
        em.add_separator()
        em.add_command(label='Конвертировать 24bpp в 16bpp', command=self.convert_to_16bpp)
        em.add_command(label='Конвертировать 16bpp в 24bpp', command=self.convert_to_24bpp)
        em.add_separator()
        em.add_command(label='Разделить изображение на два', command=self.split_image)
        em.add_command(label='Слить со следующим изображением',
                       command=lambda: self.merge_images('next'))
        em.add_command(label='Слить с предыдущим изображением',
                       command=lambda: self.merge_images('prev'))
        mb.add_cascade(label='Правка', menu=em)
        self.all_menus.append(em)

        vm = tk.Menu(mb, tearoff=0)
        vm.add_checkbutton(label='Прозрачность (STP)', variable=self.show_transparency,
                            command=self._on_transparency_toggle)
        vm.add_separator()
        vm.add_command(label='Предыдущее изображение', accelerator='Ctrl+Left',
                            command=lambda: self.switch_image(-1))
        vm.add_command(label='Следующее изображение', accelerator='Ctrl+Right',
                            command=lambda: self.switch_image(+1))
        vm.add_separator()
        vm.add_command(label='Предыдущая палитра', accelerator='Ctrl+Down',
                            command=lambda: self.palette_row_step(-1))
        vm.add_command(label='Следующая палитра', accelerator='Ctrl+Up',
                            command=lambda: self.palette_row_step(+1))
        vm.add_separator()
        vm.add_command(label='Масштаб - 1:1', accelerator='Ctrl+1',
                            command=lambda: self.preview_scale(False))
        vm.add_command(label='Масштаб - вписать в окно', accelerator='Ctrl+2',
                            command=lambda: self.preview_scale(True))
        vm.add_separator()
        vm.add_command(label='Светлая тема оформления', command=lambda: self.apply_theme('light'))
        vm.add_command(label='Тёмная тема оформления', command=lambda: self.apply_theme('dark'))
        vm.add_separator()
        vm.add_checkbutton(label='Журнал логов', variable=self.show_log_var,
                           command=self.log_window.toggle)
        mb.add_cascade(label='Вид', menu=vm)
        self.all_menus.append(vm)

        mb.add_command(label='О программе', command=self._about)
        self.root.config(menu=mb)

    def _populate_file_menu(self):
        fm = self._file_menu
        fm.delete(0, 'end')

        fm.add_command(label='Открыть TIM ...', accelerator='Ctrl+O', command=self.open_tim)
        fm.add_command(label='Открыть как сырой TIM ...', accelerator='Ctrl+Shift+O',
                       command=self.open_raw_tim)
        fm.add_command(label='Заменить изображение ...', accelerator='Ctrl+R',
                       command=self.replace_image)
        fm.add_separator()
        fm.add_command(label='Сохранить TIM', accelerator='Ctrl+S',
                       command=lambda: self.save_tim(save_as=False))
        fm.add_command(label='Сохранить TIM как ...',
                       command=lambda: self.save_tim(save_as=True))
        fm.add_command(label='Сохранить как PNG ...', accelerator='Ctrl+Shift+S',
                       command=self.save_png)
        fm.add_separator()
        fm.add_command(label='Закрыть текущий файл', accelerator='Ctrl+W',
                       command=self.close_file)
        fm.add_separator()
        fm.add_command(label='Выход', command=self._on_close_window)
        fm.add_separator()

        if self.recent_files:
            for i, path in enumerate(self.recent_files):
                label = '{}. {}'.format(i + 1, os.path.basename(path))
                if len(label) > 40:
                    label = label[:37] + ' ...'
                fm.add_command(label=label, command=lambda p=path: self._open_recent(p))
            fm.add_separator()
            fm.add_command(label='Очистить список недавних', command=self._clear_recent)
        else:
            fm.add_command(label='(нет недавних файлов)', state='disabled')

    # ============ UI construction ============

    def _build_ui(self):
        main = ttk.Frame(self.root)
        main.pack(fill=tk.BOTH, expand=True, padx=4, pady=2)
        main.columnconfigure(0, weight=1)
        main.columnconfigure(1, weight=0)
        main.rowconfigure(0, weight=1)

        left = ttk.Frame(main)
        left.grid(row=0, column=0, sticky='nsew')
        right = ttk.Frame(main)
        right.grid(row=0, column=1, sticky='ns', padx=(6, 0))

        # ----- Палитра -----
        pal_head = ttk.Frame(left)
        pal_head.pack(fill=tk.X)
        ttk.Label(pal_head, text='Палитра:', width=14, anchor='w').pack(side=tk.LEFT)
        self.pal_info_var = tk.StringVar(value='')
        ttk.Label(pal_head, textvariable=self.pal_info_var, anchor=tk.E).pack(side=tk.RIGHT)

        self.pal_prev_btn = ttk.Button(
            pal_head, text='<-', width=3,
            command=lambda: self.palette_row_step(-1))
        self.pal_prev_btn.pack(side=tk.LEFT, padx=(8, 2))

        self.pal_row_label_var = tk.StringVar(value='—')
        ttk.Label(pal_head, textvariable=self.pal_row_label_var, width=6,
                  anchor=tk.CENTER).pack(side=tk.LEFT, padx=2)

        self.pal_next_btn = ttk.Button(
            pal_head, text='->', width=3,
            command=lambda: self.palette_row_step(+1))
        self.pal_next_btn.pack(side=tk.LEFT, padx=(2, 8))

        self.palette_canvas = tk.Canvas(left, height=36, bg='#111', highlightthickness=0)
        self.palette_canvas.pack(fill=tk.X, pady=(2, 4))
        self.palette_canvas.bind('<Button-1>', self._on_palette_click)
        self.palette_canvas.bind('<Configure>', lambda e: self._refresh_palette())

        # ----- Предпросмотр -----
        prev_head = ttk.Frame(left)
        prev_head.pack(fill=tk.X)
        ttk.Label(prev_head, text='Предпросмотр:', width=14, anchor='w').pack(side=tk.LEFT)

        self.img_prev_btn = ttk.Button(
            prev_head, text='<-', width=3,
            command=lambda: self.switch_image(-1))
        self.img_prev_btn.pack(side=tk.LEFT, padx=(8, 2))

        self.nav_label_var = tk.StringVar(value='—')
        ttk.Label(prev_head, textvariable=self.nav_label_var,
                  width=6, anchor=tk.CENTER).pack(side=tk.LEFT, padx=2)

        self.img_next_btn = ttk.Button(
            prev_head, text='->', width=3,
            command=lambda: self.switch_image(+1))
        self.img_next_btn.pack(side=tk.LEFT, padx=(2, 8))

        ttk.Button(prev_head, text='1:1', width=4,
                    command=lambda: self.preview_scale(False)).pack(side=tk.RIGHT)
        ttk.Button(prev_head, text='[  ]', width=4,
                    command=lambda: self.preview_scale(True)).pack(side=tk.RIGHT, padx=2)
        self.btn_transparency = ttk.Button(prev_head, text='STP', width=4,
                    command=self._toggle_transparency_btn)
        self.btn_transparency.pack(side=tk.RIGHT, padx=(6, 0))

        self.preview_canvas = tk.Canvas(left, bg='#181818', highlightthickness=0)
        self.preview_canvas.pack(fill=tk.BOTH, expand=True)
        self.preview_canvas.bind('<Configure>', self._schedule_render_preview)

        self._build_params_panel(right)

        status_txt = 'Готово. (Поддерживается Drag & Drop файлов)' if HAS_DND else 'Готово.'
        self.status = tk.StringVar(value=status_txt)
        ttk.Label(self.root, textvariable=self.status, relief=tk.SUNKEN, anchor=tk.W,
                  padding=(4, 2)).pack(side=tk.BOTTOM, fill=tk.X)

    def _build_params_panel(self, parent):
        parent.columnconfigure(0, weight=1)
        parent.columnconfigure(1, weight=1)
        ttk.Label(parent, text='Параметры TIM').grid(
            row=0, column=0, columnspan=2, sticky='w', pady=(0, 2))

        self.v_bpp = tk.StringVar()
        self.v_has_clut = tk.BooleanVar()
        self.v_has_image = tk.BooleanVar()
        self.v_flags_display = tk.StringVar(value='0x00')
        self.v_clut = {k: tk.StringVar() for k in ('size', 'x', 'y', 'width_words', 'height')}
        self.v_image = {k: tk.StringVar() for k in ('size', 'x', 'y', 'width_words', 'height')}
        self.v_image_pixels = tk.StringVar(value='')

        # --- live-пересчёт size при ручном изменении width_words / height ---
        self.v_clut['width_words'].trace('w', self._on_clut_dims_change)
        self.v_clut['height'].trace('w', self._on_clut_dims_change)
        self.v_image['width_words'].trace('w', self._on_image_dims_change)
        self.v_image['height'].trace('w', self._on_image_dims_change)
        
        f1 = ttk.LabelFrame(parent, text='Заголовок', padding=4)
        f1.grid(row=1, column=0, sticky='nsew', pady=2, padx=(0, 3))
        ttk.Label(f1, text='bpp:').grid(row=0, column=0, sticky='w', pady=1)
        cb = ttk.Combobox(f1, textvariable=self.v_bpp, values=BPP_LABELS,
                          width=10, state='readonly')
        cb.grid(row=0, column=1, sticky='we', pady=1)
        cb.bind('<<ComboboxSelected>>', lambda e: self._update_flags_display())
        ttk.Checkbutton(f1, text='has_clut', variable=self.v_has_clut,
                        command=self._update_flags_display).grid(
            row=1, column=0, columnspan=2, sticky='w', pady=1)
        ttk.Checkbutton(f1, text='has_image', variable=self.v_has_image,
                        command=self._update_flags_display).grid(
            row=2, column=0, columnspan=2, sticky='w', pady=1)
        ttk.Label(f1, text='flags:').grid(row=3, column=0, sticky='w', pady=1)
        ttk.Label(f1, textvariable=self.v_flags_display, foreground='#888').grid(
            row=3, column=1, sticky='w', pady=1)
        f1.columnconfigure(1, weight=1)

        self.clut_frame = ttk.LabelFrame(parent, text='CLUT', padding=4)
        self.clut_frame.grid(row=2, column=0, sticky='nsew', pady=2, padx=(0, 3))
        for i, k in enumerate(('size', 'x', 'y', 'width_words', 'height')):
            ttk.Label(self.clut_frame, text=k + ':').grid(row=i, column=0, sticky='w', pady=1)
            state = 'readonly' if k == 'size' else 'normal'
            ttk.Entry(self.clut_frame, textvariable=self.v_clut[k], width=10,
                      state=state).grid(row=i, column=1, sticky='we', pady=1)
        ttk.Label(self.clut_frame, text='(size = 12 + w_w * 2 * h)',
                  foreground='#888').grid(row=5, column=0, columnspan=2, sticky='w', pady=(2, 0))
        self.clut_frame.columnconfigure(1, weight=1)

        self.image_frame = ttk.LabelFrame(parent, text='Изображение', padding=4)
        self.image_frame.grid(row=1, column=1, rowspan=2, sticky='nsew', pady=2, padx=(3, 0))
        for i, k in enumerate(('size', 'x', 'y', 'width_words', 'height')):
            ttk.Label(self.image_frame, text=k + ':').grid(row=i, column=0, sticky='w', pady=1)
            state = 'readonly' if k == 'size' else 'normal'
            ttk.Entry(self.image_frame, textvariable=self.v_image[k], width=10,
                      state=state).grid(row=i, column=1, sticky='we', pady=1)
        ttk.Label(self.image_frame, text='(size = 12 + w_w * 2 * h)',
                  foreground='#888').grid(row=5, column=0, columnspan=2, sticky='w', pady=(2, 0))
        ttk.Label(self.image_frame, textvariable=self.v_image_pixels,
                    ).grid(row=6, column=0, columnspan=2, sticky='e', pady=(0, 0))
        self.image_frame.columnconfigure(1, weight=1)
        ttk.Button(parent, text='Сбросить изменения',
                   command=self.reset_metadata).grid(row=3, column=0, sticky='we',
                                                    pady=(6, 0), padx=(0, 2))
        ttk.Button(parent, text='Применить метаданные',
                   command=self.apply_metadata).grid(row=3, column=1, sticky='we',
                                                    pady=(6, 0), padx=(2, 0))

    # ============ Theme ============

    def _theme_colors(self):
        if self.theme_mode == 'dark':
            return {
                'bg': '#2b2b2b', 'bg2': '#3a3a3a', 'bg3': '#404040',
                'fg': '#dcdcdc', 'fg_dim': '#888888', 'sel': '#3c7fb1',
                'preview_bg': '#181818', 'palette_bg': '#2b2b2b',
                'check_bg': '#4b6e9a', 'check_fg': '#ffffff',
            }
        return {
            'bg': '#f0f0f0', 'bg2': '#ffffff', 'bg3': '#e0e0e0',
            'fg': '#202020', 'fg_dim': '#666666', 'sel': '#4a90d9',
            'preview_bg': '#e8e8e8', 'palette_bg': '#f0f0f0',
            'check_bg': '#4a90d9', 'check_fg': '#ffffff',
        }

    def _add_themed_apply(self, win, apply_fn):
        entry = (win, apply_fn)
        self._themed_windows.append(entry)
        try:
            apply_fn(self._theme_colors())
        except Exception:
            pass

        def remove():
            try:
                self._themed_windows.remove(entry)
            except ValueError:
                pass
        return remove

    def _register_themed_window(self, win, apply_fn):
        remove = self._add_themed_apply(win, apply_fn)

        def _on_close():
            remove()
            try:
                win.destroy()
            except Exception:
                pass

        win.protocol('WM_DELETE_WINDOW', _on_close)
        return _on_close

    def _apply_theme_to_children(self):
        colors = self._theme_colors()
        for entry in list(self._themed_windows):
            win, apply_fn = entry
            try:
                if not win.winfo_exists():
                    self._themed_windows.remove(entry)
                    continue
                apply_fn(colors)
            except Exception:
                try:
                    self._themed_windows.remove(entry)
                except ValueError:
                    pass


    def apply_theme(self, mode):
        self.theme_mode = mode
        self.config['theme'] = mode
        style = ttk.Style(self.root)
        style.theme_use('clam')

        c = self._theme_colors()
        bg, bg2, bg3 = c['bg'], c['bg2'], c['bg3']
        fg, fg_dim = c['fg'], c['fg_dim']
        sel = c['sel']
        preview_bg, palette_bg = c['preview_bg'], c['palette_bg']

        style.configure('.', background=bg, foreground=fg, fieldbackground=bg2,
                        bordercolor='#888888', troughcolor=bg3, selectbackground=sel,
                        selectforeground='#ffffff', lightcolor=bg3, darkcolor=bg3)
        style.configure('TFrame', background=bg)
        style.configure('TLabel', background=bg, foreground=fg)
        style.configure('TLabelFrame', background=bg, foreground=fg)
        style.configure('TLabelFrame.Label', background=bg, foreground=fg)
        style.configure('TButton', background=bg3, foreground=fg, bordercolor='#888888')
        style.map('TButton', background=[('active', sel), ('pressed', sel)],
                  foreground=[('active', '#ffffff')])
        style.configure('TEntry', fieldbackground=bg2, foreground=fg, insertcolor=fg)
        style.configure('TCheckbutton', background=bg, foreground=fg)
        style.map('TCheckbutton', background=[('active', bg)])
        style.configure('TSpinbox', fieldbackground=bg2, foreground=fg, arrowcolor=fg)
        style.configure('TCombobox', fieldbackground=bg2, foreground=fg, arrowcolor=fg)
        style.map('TCombobox', fieldbackground=[('readonly', bg2)])
        style.configure('TPanedwindow', background=bg)
        style.configure('TSeparator', background='#888888')
        style.configure('TScrollbar', background=bg3, troughcolor=bg, arrowcolor=fg)

        self.root.configure(bg=bg)
        self.preview_canvas.configure(bg=preview_bg)
        self.palette_canvas.configure(bg=palette_bg)

        for menu in self.all_menus:
            try:
                menu.configure(bg=bg, fg=fg, activebackground=sel, activeforeground='#ffffff',
                               selectcolor=c['check_bg'], disabledforeground=fg_dim,
                               activeborderwidth=1, bd=1)
            except Exception:
                pass
        self._apply_theme_to_children()
        self._update_nav_buttons()

    # ============ Recent files ============

    def _add_recent_file(self, path):
        if not path:
            return
        try:
            path = os.path.abspath(path)
        except Exception:
            return
        if not os.path.isfile(path):
            log.warning('Не добавляю в недавние несуществующий файл: %s', path)
            return
        if path in self.recent_files:
            self.recent_files.remove(path)
        self.recent_files.insert(0, path)
        self.recent_files = self.recent_files[:_RECENT_LIMIT]
        self.config['recent_files'] = list(self.recent_files)
        self._update_recent_menu()

    def _update_recent_menu(self):
        if hasattr(self, '_file_menu'):
            self._populate_file_menu()

    def _open_recent(self, path):
        if not os.path.isfile(path):
            log.warning('Файл из списка недавних не найден: %s', path)
            messagebox.showwarning(
                'Файл не найден',
                'Файл больше не существует:\n{}'.format(path))
            if path in self.recent_files:
                self.recent_files.remove(path)
                self.config['recent_files'] = list(self.recent_files)
                self._update_recent_menu()
            return
        self._load_tim_from_path(path)

    def _clear_recent(self):
        if not self.recent_files:
            return
        self.recent_files = []
        self.config['recent_files'] = []
        self._update_recent_menu()

    # ============ Config helpers ============

    def _initial_dir(self):
        return self.config.get('last_dir', '')

    def _remember_dir(self, path):
        self.config['last_dir'] = os.path.dirname(path)

    def _on_auto_save_change(self, fmt):
        value = self.auto_save_pal[fmt].get()
        self.config['auto_save_{}_with_png'.format(fmt)] = value

    # ============ File open ============

    def _on_file_drop(self, event):
        try:
            paths = self.root.tk.splitlist(event.data)
        except Exception:
            paths = []
        if not paths:
            return
        path = paths[0]
        if len(paths) > 1:
            log.info('Перетащено файлов: %d, обрабатывается первый: %s',
                     len(paths), os.path.basename(path))
        self._dispatch_dropped_file(path)

    def _dispatch_dropped_file(self, path):
        if not os.path.isfile(path):
            log.warning('Перетащенный путь не является файлом: %s', path)
            return
        log.info('Перетащен файл: %s', os.path.basename(path))
        ext = os.path.splitext(path)[1].lower()

        if ext in _DROP_IMAGE_EXTS:
            self._replace_image_from_path(path)
            return

        if ext in _DROP_PALETTE_EXTS:
            self._import_palette_from_path(path)
            return

        if getattr(self, '_batch_running', False):
            self._set_status('Дождитесь завершения пакетной операции')
            return
        if not self._confirm_discard():
            log.info('Открытие отменено пользователем (несохранённые изменения)')
            return
        if self._load_tim_from_path(path, silent=True):
            return
        log.info('Не удалось открыть как TIM, пробую как сырой TIM: %s',
                 os.path.basename(path))
        self._load_raw_tim_from_path(path, skip_confirm=True, silent=True)

    def open_tim(self):
        path = filedialog.askopenfilename(title='Открыть TIM',
                                          initialdir=self._initial_dir(),
                                          filetypes=[('Все файлы', '*.*'), ('TIM', '*.tim')])
        if path:
            self._load_tim_from_path(path)

    def open_raw_tim(self):
        path = filedialog.askopenfilename(title='Открыть как сырой TIM',
                                          initialdir=self._initial_dir(),
                                          filetypes=[('Все файлы', '*.*')])
        if path:
            self._load_raw_tim_from_path(path)

    def _load_tim_from_path(self, path, silent=False):
        if getattr(self, '_batch_running', False):
            if not silent:
                messagebox.showinfo('Информация', 'Дождитесь завершения пакетной операции.')
            return False
        if not silent:
            if not self._confirm_discard():
                log.info('Открытие отменено пользователем (несохранённые изменения)')
                return False
        log.info('Открытие TIM: %s', path)
        try:
            with open(path, 'rb') as f:
                data = f.read()
            self.groups = parse_tims(data)
            self.entries = make_entries(self.groups)
            self.current_entry = 0 if self.entries else -1
            self.palette_row = 0
            self.source_stem = os.path.splitext(os.path.basename(path))[0]
            self.source_path = path
            self._remember_dir(path)
            self._add_recent_file(path)
            self.zoom_fit = bool(self.config.get('zoom_fit', True))
            self._mark_dirty(False)
            self._refresh_all()

            warns = [w for g in self.groups for w in g.get('warnings', [])]
            msg = 'Открыто: {} | групп: {} | изобр.: {}'.format(
                os.path.basename(path), len(self.groups), len(self.entries))
            if warns:
                msg += '  [!] ' + '; '.join(warns)
            self._set_status(msg)

            log.info('Открыто: %s (групп: %d, изобр.: %d, размер: %d байт)',
                     os.path.basename(path), len(self.groups), len(self.entries), len(data))
            for w in warns:
                log.warning('TIM: %s', w)
            return True
        except Exception as e:
            log.error('Не удалось открыть TIM %s: %s', os.path.basename(path), e)
            if not silent:
                messagebox.showerror('Ошибка', str(e))
            return False

    def _load_raw_tim_from_path(self, path, skip_confirm=False, silent=False):
        if getattr(self, '_batch_running', False):
            if not silent:
                messagebox.showinfo('Информация', 'Дождитесь завершения пакетной операции.')
            return False
        if not skip_confirm:
            if not self._confirm_discard():
                log.info('Открытие отменено пользователем (несохранённые изменения)')
                return False

        log.info('Открытие как сырой TIM: %s', os.path.basename(path))
        try:
            file_size = os.path.getsize(path)
            if file_size > 200 * 1024 * 1024:
                if not messagebox.askyesno(
                        'Большой файл',
                        'Файл очень большой ({} MB).\n'
                        'Открытие может занять много времени.\n\nПродолжить?'.format(
                            file_size // (1024 * 1024))):
                    log.info('Открытие большого файла отменено пользователем')
                    if silent:
                        self._set_status('Открытие большого файла отменено')
                    return False

            with open(path, 'rb') as f:
                data = f.read()

            if len(data) < 8:
                raise ValueError('Файл слишком мал')

            groups = []
            parse_success = False

            try:
                group, _ = parse_tim_group(data, 0, check_signature=False)
                has_images = bool(group.get('images'))
                has_clut = bool(group.get('clut_header') and group.get('clut'))
                if has_images or has_clut:
                    groups.append(group)
                    group['warnings'].append('Открыт как сырой TIM (без проверки сигнатуры)')
                    parse_success = True
            except Exception as e:
                log.warning('Сырой парсинг не удался: %s', e)

            if not parse_success:
                if silent:
                    log.warning('Сырой TIM не распознан (silent): %s', os.path.basename(path))
                    self._set_status('Не удалось распознать TIM: {}'.format(
                        os.path.basename(path)))
                    return False

                if not messagebox.askyesno(
                        'Файл не распознан',
                        'Файл не может быть распознан как TIM.\n'
                        'Попытаться открыть?\n\n'
                        'Да — создать пустую структуру для ручного ввода метаданных\n'
                        'Нет — вернуться'):
                    log.info('Открытие сырого TIM отменено')
                    return False

                group = {
                    'flags': 0x18, 'bpp': 0,
                    'has_clut': True, 'has_image': True,
                    'clut_header': {'size': 44, 'x': 0, 'y': 480,
                                    'width_words': 16, 'height': 1},
                    'clut': b'\x00' * 32,
                    'images': [{
                        'header': {'size': 12, 'x': 0, 'y': 0,
                                   'width_words': 16, 'height': 1},
                        'data': b''
                    }],
                    'extra': data,
                    'warnings': ['Создана пустая структура для ручного ввода метаданных'],
                    'start_offset': 0,
                }
                groups.append(group)

            self.groups = groups
            self.entries = make_entries(self.groups)
            self.current_entry = 0 if self.entries else -1
            self.palette_row = 0
            self.source_stem = os.path.splitext(os.path.basename(path))[0]
            self.source_path = path
            self._remember_dir(path)
            self.zoom_fit = bool(self.config.get('zoom_fit', True))
            self._mark_dirty(False)
            self._refresh_all()

            warns = [w for g in self.groups for w in g.get('warnings', [])]
            if parse_success:
                msg = 'Открыт сырой TIM: {} | групп: {} | изобр.: {}'.format(
                    os.path.basename(path), len(self.groups), len(self.entries))
                log.info('Открыт сырой TIM: %s (групп: %d, изобр.: %d)',
                         os.path.basename(path), len(self.groups), len(self.entries))
            else:
                msg = 'Создана пустая структура: {} | Введите метаданные вручную'.format(
                    os.path.basename(path))
                log.info('Создана пустая структура для %s', os.path.basename(path))

            if warns:
                msg += '  [!] ' + '; '.join(warns)
                for w in warns:
                    log.warning('TIM: %s', w)
            self._set_status(msg)
            return True

        except Exception as e:
            log.error('Не удалось открыть сырой TIM %s: %s', os.path.basename(path), e)
            if not silent:
                messagebox.showerror('Ошибка', str(e))
            else:
                self._set_status('Не удалось открыть файл: {}'.format(os.path.basename(path)))
            return False

    def close_file(self):
        if not self.groups or not self._confirm_discard():
            return
        log.info('Закрытие файла: %s',
                 os.path.basename(self.source_path) if self.source_path else '(без имени)')
        self.groups, self.entries = [], []
        self.current_entry = -1
        self.source_stem = self.source_path = None
        self.palette_row = 0
        self.preview_image = None
        self.preview_canvas.delete('all')
        self.palette_canvas.delete('all')
        self.pal_info_var.set('')
        try:
            self.v_bpp.set('')
            self.v_has_clut.set(False)
            self.v_has_image.set(False)
            self.v_flags_display.set('0x00')
            for k in self.v_clut: self.v_clut[k].set('')
            for k in self.v_image: self.v_image[k].set('')
            self.clut_frame.configure(text='CLUT')
            self.image_frame.configure(text='Изображение')
        except Exception:
            pass
        self._update_nav_buttons()
        self._mark_dirty(False)
        self._set_status('Файл закрыт.')

    def reset_metadata(self):
        if not self.source_path:
            return
        log.info('Сброс метаданных — перечитывание %s', os.path.basename(self.source_path))
        self._load_tim_from_path(self.source_path, silent=True)
        self._set_status('Метаданные сброшены (файл перечитан)')

    # ============ File save ============

    def save_tim(self, save_as=True):
        if getattr(self, '_batch_running', False):
            messagebox.showinfo('Информация', 'Дождитесь завершения пакетной операции.')
            return 'cancelled'
        if not self.groups:
            messagebox.showinfo('Информация', 'Нет данных.')
            return 'cancelled'

        if not save_as and self.source_path:
            path = self.source_path
        else:
            default = self.source_stem or 'untitled'
            if save_as:
                default += '_out'
            path = filedialog.asksaveasfilename(
                title='Сохранить TIM как ...' if save_as else 'Сохранить TIM',
                initialfile=default, initialdir=self._initial_dir(),
                filetypes=[('Все файлы', '*.*'), ('TIM', '*.tim')])
            if not path:
                return 'cancelled'

        try:
            data = rebuild_all(self.groups)
            with open(path, 'wb') as f:
                f.write(data)
            self.source_path = path
            self.source_stem = os.path.splitext(os.path.basename(path))[0]
            self._remember_dir(path)
            self._mark_dirty(False)
            log.info('Сохранено: %s (%d байт)', path, len(data))
            self._set_status('Сохранено: {} ({} байт)'.format(path, len(data)))
            return 'saved'
        except Exception as e:
            log.error('Не удалось сохранить TIM %s: %s', path, e)
            messagebox.showerror('Ошибка', str(e))
            return 'cancelled'

    def save_png(self):
        if getattr(self, '_batch_running', False):
            messagebox.showinfo('Информация', 'Дождитесь завершения пакетной операции.')
            return
        group, ii = self._current()
        if group is None:
            messagebox.showinfo('Информация', 'Нет TIM.')
            return
        try:
            img = decode_image(group, ii if ii is not None else -1, self.palette_row)
        except Exception as e:
            log.error('Не удалось декодировать для PNG: %s', e)
            messagebox.showerror('Ошибка', str(e))
            return

        stem = self.source_stem or 'image'
        if len(self.entries) > 1:
            if ii is not None and ii >= 0:
                default = '{}_img{:02d}.png'.format(stem, ii + 1)
            else:
                default = '{}_palette.png'.format(stem)
        else:
            default = stem + '.png'

        path = filedialog.asksaveasfilename(
            title='Сохранить PNG', initialfile=default, defaultextension='.png',
            initialdir=self._initial_dir(),
            filetypes=[('PNG', '*.png'), ('BMP', '*.bmp'), ('Все файлы', '*.*')])
        if not path:
            return

        try:
            if group['bpp'] == 0:
                try:
                    img.save(path, bits=4)
                except Exception:
                    img.save(path)
            else:
                img.save(path)
            self._remember_dir(path)

            base_path, _ = os.path.splitext(path)
            saved_extras = self._auto_save_palettes_for_group(group, base_path)
            if saved_extras:
                log.info('Сохранено: %s (палитры: %s)', path, ', '.join(saved_extras))
                self._set_status('Сохранено: {} (и палитры: {})'.format(
                    path, ', '.join(saved_extras)))
            else:
                log.info('Сохранено: %s', path)
                self._set_status('Сохранено: {}'.format(path))
        except Exception as e:
            log.error('Не удалось сохранить PNG %s: %s', path, e)
            messagebox.showerror('Ошибка', str(e))

    def _confirm_discard(self):
        if getattr(self, '_batch_running', False):
            messagebox.showinfo('Информация', 'Дождитесь завершения пакетной операции.')
            return False
        if not self.dirty:
            return True
        answer = messagebox.askyesnocancel(
            'Несохранённые изменения',
            'Сохранить изменения в TIM файле?\n\n'
            'Да — сохранить \nНет — не сохранять\nОтмена — вернуться')
        if answer is None:
            log.info('Пользователь отменил действие (есть несохранённые изменения)')
            return False
        if answer:
            return self.save_tim(save_as=False) == 'saved'
        return True

    def _on_close_window(self):
        if getattr(self, '_batch_running', False):
            messagebox.showinfo('Информация', 'Дождитесь завершения пакетной операции.')
            return
        if not self._confirm_discard():
            return
        log.info('Выход из приложения')
        try:
            state = self.root.state()
            self.config['window_state'] = state
            if state == 'normal':
                self.config['window_geometry'] = self.root.geometry()
        except Exception:
            pass
        self.config['zoom_fit'] = bool(self.zoom_fit)
        self.config['show_log'] = bool(self.show_log_var.get())
        self.config['show_transparency'] = bool(self.show_transparency.get())
        save_config(self.config)
        try:
            self.log_window.close()
        except Exception:
            pass
        self.root.destroy()

    # ============ Status / title ============

    def _set_status(self, msg):
        self.status.set(msg)

    def _update_title(self):
        mark = ' *' if self.dirty else ''
        if self.source_path:
            self.root.title('{} — {}{}'.format(
                MAIN_TITLE, os.path.basename(self.source_path), mark))
        elif self.source_stem:
            self.root.title('{} — {} (не сохранён){}'.format(
                MAIN_TITLE, self.source_stem, mark))
        else:
            self.root.title(MAIN_TITLE)

    def _mark_dirty(self, is_dirty=True):
        self.dirty = is_dirty
        self._update_title()

    def _update_flags_display(self, *args):
        try:
            bpp = BPP_LABELS.index(self.v_bpp.get())
            flags = bpp | (0x8 if self.v_has_clut.get() else 0) \
                        | (0x10 if self.v_has_image.get() else 0)
            self.v_flags_display.set('0x{:02X}'.format(flags))
        except Exception:
            self.v_flags_display.set('?')
        
        self._update_image_pixels_label()

    def _update_image_pixels_label(self):
        """Обновляет подпись под полями «Изображение»: размер в пикселях по bpp."""
        try:
            bpp = BPP_LABELS.index(self.v_bpp.get())
        except Exception:
            self.v_image_pixels.set('')
            return
        try:
            ww_s = self.v_image['width_words'].get().strip()
            hh_s = self.v_image['height'].get().strip()
            if not ww_s or not hh_s:
                self.v_image_pixels.set('')
                return
            ww = parse_int_flexible(ww_s)
            hh = parse_int_flexible(hh_s)
            if ww <= 0 or hh <= 0:
                self.v_image_pixels.set('')
                return
            px_w = image_width_words_to_pixels(bpp, ww)
            self.v_image_pixels.set('    {} * {} px'.format(px_w, hh))
        except Exception:
            self.v_image_pixels.set('')
        
    # ============ Navigation ============

    def _current(self):
        if self.current_entry < 0 or self.current_entry >= len(self.entries):
            return None, None
        gi, ii = self.entries[self.current_entry]
        return self.groups[gi], ii

    def switch_image(self, delta):
        if not self.entries: return
        new_idx = self.current_entry + delta
        new_idx = max(0, min(new_idx, len(self.entries) - 1))
        if new_idx == self.current_entry: return
        self.current_entry = new_idx
        self._refresh_all()      

    def palette_row_step(self, delta):
        group, _ = self._current()
        if group is None or not group.get('clut_header'):
            return
        max_row = max(0, (group['clut_header']['height'] or 1) - 1)
        new = max(0, min(self.palette_row + delta, max_row))
        if new != self.palette_row:
            self.palette_row = new
            self._on_pal_row_change()

    # ============ View / preview ============
    def _schedule_render_preview(self, event=None):
        if self._render_after_id is not None:
            try:
                self.root.after_cancel(self._render_after_id)
            except Exception:
                pass
        self._render_after_id = self.root.after(60, self._do_render_preview)

    def _do_render_preview(self):
        self._render_after_id = None
        self._render_preview()
    
    def preview_scale(self, fit=True):
        if self.zoom_fit == fit: return
        self.zoom_fit = fit
        self.config['zoom_fit'] = fit
        self._render_preview()

    def _on_transparency_toggle(self):
        self.config['show_transparency'] = bool(self.show_transparency.get())
        log.info('Прозрачность (STP): %s',
                 'вкл' if self.show_transparency.get() else 'выкл')
        self._refresh_preview()

    def _toggle_transparency_btn(self):
        self.show_transparency.set(not self.show_transparency.get())
        self._on_transparency_toggle()

    def _render_preview(self):
        img = self.preview_image
        c = self.preview_canvas
        c.delete('all')
        if img is None:
            return
        cw, ch = c.winfo_width(), c.winfo_height()
        if cw < 2 or ch < 2:
            return
        iw, ih = img.size
        if iw <= 0 or ih <= 0:
            return

        scale = min(cw / iw, ch / ih) if self.zoom_fit else 1.0
        nw, nh = max(1, int(iw * scale)), max(1, int(ih * scale))
        disp = img.resize((nw, nh), Image.NEAREST) if (nw, nh) != (iw, ih) else img

        bg = make_checkerboard(nw, nh, size=max(4, int(8 * scale)) if scale >= 1 else 4)
        if disp.mode == 'RGBA':
            bg.paste(disp, (0, 0), disp)
        else:
            bg.paste(disp, (0, 0))

        self.preview_photo = ImageTk.PhotoImage(bg)
        c.create_image(4, 4, image=self.preview_photo, anchor='nw')

    def _refresh_preview(self):
        group, ii = self._current()
        if group is None:
            self.preview_image = None
            self.preview_canvas.delete('all')
            return
        try:
            alpha_stp = bool(self.show_transparency.get())
            self.preview_image = decode_image(
                group, ii if ii is not None else -1, self.palette_row,
                alpha_stp=alpha_stp)
            self._render_preview()
        except Exception as e:
            log.error('Ошибка предпросмотра: %s', e)
            self._set_status('Ошибка предпросмотра: {}'.format(e))

    def _refresh_palette(self):
        self.palette_canvas.delete('all')
        self._palette_cells = []
        group, _ = self._current()
        if group is None:
            self.pal_info_var.set('')
            self._pal_photo = None
            return
        if not (group.get('clut') and group.get('clut_header')):
            self.pal_info_var.set('CLUT отсутствует')
            self._pal_photo = None
            return

        ch_count = group['clut_header']['height'] or 1
        if self.palette_row >= ch_count:
            self.palette_row = ch_count - 1
        if self.palette_row < 0:
            self.palette_row = 0

        palette = get_clut_palette(group, self.palette_row)
        if not palette:
            self.pal_info_var.set('Палитра пуста')
            self._pal_photo = None
            return

        cw = max(self.palette_canvas.winfo_width(), 100)
        chh = max(self.palette_canvas.winfo_height(), 20)
        n = len(palette)
        sw = max(1, cw // n)

        img = Image.new('RGB', (sw * n, chh))
        px = img.load()
        for i, (r, g, b) in enumerate(palette):
            x0 = i * sw
            for yy in range(chh):
                for xx in range(x0, x0 + sw):
                    px[xx, yy] = (r, g, b)

        self._pal_photo = ImageTk.PhotoImage(img)
        self.palette_canvas.create_image(0, 0, image=self._pal_photo, anchor='nw')

        self._palette_cells = []
        cw_w = group['clut_header']['width_words']
        for i, (r, g, b) in enumerate(palette):
            off = (self.palette_row * cw_w + i) * 2
            try:
                c_val = struct.unpack('<H', group['clut'][off:off + 2])[0]
            except Exception:
                c_val = 0
            self._palette_cells.append((i * sw, (i + 1) * sw, (r, g, b), i, c_val))

        self.pal_info_var.set('{} стр. / {} цветов'.format(ch_count, n))

    def _on_image_dims_change(self, *_):
        """Live-пересчёт поля size при ручном изменении width_words / height."""
        if getattr(self, '_suppress_size_trace', False):
            return
        try:
            ww_s = self.v_image['width_words'].get().strip()
            hh_s = self.v_image['height'].get().strip()
            if not ww_s or not hh_s:
                return
            ww = parse_int_flexible(ww_s)
            hh = parse_int_flexible(hh_s)
            if ww < 0 or hh < 0:
                return
            self.v_image['size'].set(str(12 + ww * hh * 2))
        except Exception:
            pass
        
        self._update_image_pixels_label()


    def _on_clut_dims_change(self, *_):
        """Live-пересчёт поля size CLUT при ручном изменении width_words / height."""
        if getattr(self, '_suppress_size_trace', False):
            return
        try:
            ww_s = self.v_clut['width_words'].get().strip()
            hh_s = self.v_clut['height'].get().strip()
            if not ww_s or not hh_s:
                return
            ww = parse_int_flexible(ww_s)
            hh = parse_int_flexible(hh_s)
            if ww < 0 or hh < 0:
                return
            self.v_clut['size'].set(str(12 + ww * hh * 2))
        except Exception:
            pass

    def _refresh_params(self):
        group, ii = self._current()
        self._suppress_size_trace = True
        try:
            if group is None:
                self.v_bpp.set('')
                self.v_has_clut.set(False)
                self.v_has_image.set(False)
                self.v_flags_display.set('0x00')
                for k in self.v_clut:
                    self.v_clut[k].set('')
                for k in self.v_image:
                    self.v_image[k].set('')
                try:
                    self.clut_frame.configure(text='CLUT')
                    self.image_frame.configure(text='Изображение')
                except Exception:
                    pass
                return

            bpp = group['bpp']
            self.v_bpp.set(BPP_LABELS[bpp] if 0 <= bpp < len(BPP_LABELS) else str(bpp))
            self.v_has_clut.set(bool(group.get('has_clut')))
            self.v_has_image.set(bool(group.get('has_image')))
            self._update_flags_display()

            ch = group.get('clut_header')
            if ch:
                self.clut_frame.configure(text='CLUT')
                for k in ('size', 'x', 'y', 'width_words', 'height'):
                    self.v_clut[k].set(str(ch.get(k, '')))
            else:
                self.clut_frame.configure(text='CLUT (нет)')
                for k in self.v_clut:
                    self.v_clut[k].set('')

            if ii is not None and 0 <= ii < len(group['images']):
                ih = group['images'][ii]['header']
                self.image_frame.configure(
                    text='Изображение {} из {}'.format(ii + 1, len(group['images'])))
                for k in ('size', 'x', 'y', 'width_words', 'height'):
                    self.v_image[k].set(str(ih.get(k, '')))
            else:
                self.image_frame.configure(text='Изображение (нет)')
                for k in self.v_image:
                    self.v_image[k].set('')
        finally:
            self._suppress_size_trace = False
        self._update_image_pixels_label()

    def _refresh_all(self):
        self._refresh_palette()
        self._refresh_preview()
        self._refresh_params()
        self._update_nav_buttons()

    # ============ Palette widget handlers ============

    def _on_palette_click(self, event):
        for x0, x1, rgb, idx, c_val in self._palette_cells:
            if x0 <= event.x < x1:
                self._set_status(
                    'Цвет[{}]: RGB=({},{},{}) BGR555=0x{:04X} #{:02x}{:02x}{:02x}'.format(
                        idx, rgb[0], rgb[1], rgb[2], c_val, rgb[0], rgb[1], rgb[2]))
                return
    
    def _update_nav_buttons(self):
        """Обновляет метки N/M и доступность кнопок навигации (палитра + изображения)."""
        # ----- Палитра -----
        group, _ = self._current()
        total_pal = 0
        if group is not None and group.get('clut_header'):
            total_pal = max(1, group['clut_header']['height'] or 1)

        if total_pal <= 0:
            self.pal_row_label_var.set('—')
            try:
                self.pal_prev_btn.configure(state='disabled')
                self.pal_next_btn.configure(state='disabled')
            except Exception:
                pass
        else:
            cur = max(0, min(self.palette_row, total_pal - 1))
            self.pal_row_label_var.set('{}/{}'.format(cur + 1, total_pal))
            try:
                self.pal_prev_btn.configure(
                    state='normal' if cur > 0 else 'disabled')
                self.pal_next_btn.configure(
                    state='normal' if cur < total_pal - 1 else 'disabled')
            except Exception:
                pass

        # ----- Изображения -----
        n = len(self.entries)
        idx = self.current_entry
        if n <= 0 or idx < 0:
            self.nav_label_var.set('—')
            try:
                self.img_prev_btn.configure(state='disabled')
                self.img_next_btn.configure(state='disabled')
            except Exception:
                pass
        else:
            self.nav_label_var.set('{}/{}'.format(idx + 1, n))
            try:
                self.img_prev_btn.configure(
                    state='normal' if idx > 0 else 'disabled')
                self.img_next_btn.configure(
                    state='normal' if idx < n - 1 else 'disabled')
            except Exception:
                pass
    
    def _on_pal_row_change(self):
        self._refresh_palette()
        self._refresh_preview()
        self._update_nav_buttons()

    # ============ Editing ============
    def apply_metadata(self):
        group, ii = self._current()
        if group is None:
            return

        try:
            bpp = BPP_LABELS.index(self.v_bpp.get())
        except Exception:
            log.warning('Применение метаданных: неверное значение bpp')
            messagebox.showerror('Ошибка', 'Неверное значение bpp.')
            return

        flags = bpp | (0x8 if self.v_has_clut.get() else 0) \
                    | (0x10 if self.v_has_image.get() else 0)
        group['flags'] = flags
        group['bpp'] = bpp
        group['has_clut'] = bool(self.v_has_clut.get())
        group['has_image'] = bool(self.v_has_image.get())

        if group.get('clut_header') is not None:
            for k in ('x', 'y', 'width_words', 'height'):
                v = self.v_clut[k].get().strip()
                if not v:
                    continue
                try:
                    group['clut_header'][k] = parse_int_flexible(v)
                except ValueError as e:
                    messagebox.showerror('Ошибка', 'CLUT.{}: {}'.format(k, e))
                    return
            ch = group['clut_header']
            ch['size'] = 12 + ch['width_words'] * (ch['height'] or 1) * 2

        if ii is not None and 0 <= ii < len(group['images']):
            for k in ('x', 'y', 'width_words', 'height'):
                v = self.v_image[k].get().strip()
                if not v:
                    continue
                try:
                    group['images'][ii]['header'][k] = parse_int_flexible(v)
                except ValueError as e:
                    messagebox.showerror('Ошибка', 'Изображение.{}: {}'.format(k, e))
                    return
            ih = group['images'][ii]['header']
            ih['size'] = 12 + ih['width_words'] * ih['height'] * 2

        self._rebuild_and_reparse(preserve_entry=True)
        self._mark_dirty(True)
        warns = [w for g in self.groups for w in g.get('warnings', [])]
        msg = 'Метаданные применены.'
        if warns:
            msg += '  [!] ' + '; '.join(warns)
            for w in warns:
                log.warning('TIM: %s', w)
        log.info('Метаданные применены (bpp=%d, flags=0x%02X)', bpp, flags)
        self._set_status(msg)
    
    def _rebuild_and_reparse(self, preserve_entry=True):
        old = self.current_entry if preserve_entry else 0
        try:
            data = rebuild_all(self.groups)
            self.groups = parse_tims(data)
            self.entries = make_entries(self.groups)
            self.current_entry = max(0, min(old, len(self.entries) - 1)) if self.entries else -1
            self._refresh_all()
        except Exception as e:
            log.error('Не удалось пересобрать TIM: %s', e)
            messagebox.showerror('Ошибка', 'Не удалось пересобрать TIM:\n{}'.format(e))

    def convert_bpp(self, target_bpp):
        group, ii = self._current()
        if group is None or ii is None:
            messagebox.showinfo('Информация', 'Нет изображения для конвертации.')
            return

        current_bpp = group['bpp']
        if current_bpp == target_bpp:
            messagebox.showinfo('Информация',
                                'Изображение уже в формате {}bpp.'.format(target_bpp * 8))
            return

        if current_bpp not in (2, 3) or target_bpp not in (2, 3):
            messagebox.showerror('Ошибка',
                                 'Конвертация поддерживается только между 16bpp и 24bpp.')
            return

        old_width = image_width_words_to_pixels(
            current_bpp, group['images'][ii]['header']['width_words'])
        height = group['images'][ii]['header']['height']

        try:
            img = decode_image(group, ii, self.palette_row)
        except Exception as e:
            log.error('Не удалось декодировать изображение: %s', e)
            messagebox.showerror('Ошибка',
                                 'Не удалось декодировать изображение: {}'.format(e))
            return

        group['bpp'] = target_bpp
        group['flags'] = (group['flags'] & ~0x7) | target_bpp

        if target_bpp == 2:
            new_width_words = old_width
        else:
            new_width_words = (old_width * 3 + 1) // 2

        group['images'][ii]['header']['width_words'] = new_width_words

        try:
            new_data = encode_image(img, group, group['images'][ii]['header'],
                                    self.palette_row)
            group['images'][ii]['data'] = new_data
            group['images'][ii]['header']['size'] = 12 + len(new_data)
        except Exception as e:
            log.error('Не удалось закодировать изображение: %s', e)
            messagebox.showerror('Ошибка',
                                 'Не удалось закодировать изображение: {}'.format(e))
            group['bpp'] = current_bpp
            group['flags'] = (group['flags'] & ~0x7) | current_bpp
            return

        self._rebuild_and_reparse(preserve_entry=True)
        self._mark_dirty(True)
        log.info('Конвертация bpp: %dbpp → %dbpp (%dx%d)',
                 current_bpp * 8, target_bpp * 8, old_width, height)
        self._set_status('Изображение конвертировано: {}bpp → {}bpp'.format(
            current_bpp * 8, target_bpp * 8))

    def convert_to_16bpp(self):
        self.convert_bpp(2)

    def convert_to_24bpp(self):
        self.convert_bpp(3)

    def split_image(self):
        group, ii = self._current()
        if group is None or ii is None:
            messagebox.showinfo('Информация', 'Нет изображения для разделения.')
            return

        img_data = group['images'][ii]
        bpp = group['bpp']
        width_words = img_data['header']['width_words']
        height = img_data['header']['height']
        width = image_width_words_to_pixels(bpp, width_words)

        if width < 2:
            messagebox.showerror('Ошибка', 'Изображение слишком узкое для разделения.')
            return
        if width % 2 != 0:
            messagebox.showerror('Ошибка',
                                 'Ширина изображения должна быть четной для разделения.')
            return

        half_width = width // 2
        half_width_words = pixels_to_words(bpp, half_width)
        actual_half = image_width_words_to_pixels(bpp, half_width_words)

        if actual_half != half_width:
            messagebox.showerror(
                'Ошибка',
                'Невозможно разделить без потерь.\n\n'
                'Изображение: {} пикс. ({} слов, {}bpp).\n'
                'Половина: {} пикс., но при кодировании получится {} пикс.\n\n'
                'Попробуйте конвертировать в другой bpp.'.format(
                    width, width_words, bpp * 8 if bpp else 4,
                    half_width, actual_half))
            return

        if not messagebox.askyesno(
                'Разделение изображения',
                'Разделить изображение {}x{} на две половины {}x{}?'.format(
                    width, height, half_width, height)):
            return

        try:
            full_img = decode_image(group, ii, self.palette_row)
        except Exception as e:
            log.error('Не удалось декодировать изображение: %s', e)
            messagebox.showerror('Ошибка',
                                 'Не удалось декодировать изображение: {}'.format(e))
            return

        left_img = full_img.crop((0, 0, half_width, height))
        right_img = full_img.crop((half_width, 0, width, height))

        try:
            left_header = img_data['header'].copy()
            left_header['width_words'] = half_width_words
            left_data = encode_image(left_img, group, left_header, self.palette_row)
            left_header['size'] = 12 + len(left_data)
        except Exception as e:
            log.error('Не удалось закодировать левую половинку: %s', e)
            messagebox.showerror('Ошибка',
                                 'Не удалось закодировать левую половинку: {}'.format(e))
            return

        try:
            right_header = img_data['header'].copy()
            right_header['width_words'] = half_width_words
            right_data = encode_image(right_img, group, right_header, self.palette_row)
            right_header['size'] = 12 + len(right_data)
        except Exception as e:
            log.error('Не удалось закодировать правую половинку: %s', e)
            messagebox.showerror('Ошибка',
                                 'Не удалось закодировать правую половинку: {}'.format(e))
            return

        group['images'].pop(ii)
        group['images'].insert(ii, {'header': left_header, 'data': left_data})
        group['images'].insert(ii + 1, {'header': right_header, 'data': right_data})

        self._rebuild_and_reparse(preserve_entry=True)
        self._mark_dirty(True)
        log.info('Изображение разделено: %dx%d → два %dx%d',
                 width, height, half_width, height)
        self._set_status('Изображение разделено на две половины.')

    def merge_images(self, direction='next'):
        group, ii = self._current()
        if group is None or ii is None:
            messagebox.showinfo('Информация', 'Нет изображения для слияния.')
            return

        if direction == 'next':
            if ii + 1 >= len(group['images']):
                messagebox.showinfo('Информация',
                                    'Нет следующего изображения для слияния.')
                return
            idx1, idx2 = ii, ii + 1
        else:
            if ii == 0:
                messagebox.showinfo('Информация',
                                    'Это первое изображение — предыдущего нет.')
                return
            idx1, idx2 = ii - 1, ii

        img1 = group['images'][idx1]
        img2 = group['images'][idx2]
        bpp = group['bpp']

        width1 = image_width_words_to_pixels(bpp, img1['header']['width_words'])
        width2 = image_width_words_to_pixels(bpp, img2['header']['width_words'])
        height1 = img1['header']['height']
        height2 = img2['header']['height']

        if height1 != height2:
            messagebox.showerror('Ошибка',
                                 'Высота изображений должна совпадать для слияния.')
            return

        total_width = width1 + width2
        new_width_words = pixels_to_words(bpp, total_width)
        actual_total = image_width_words_to_pixels(bpp, new_width_words)

        if actual_total != total_width:
            messagebox.showerror(
                'Ошибка',
                'Невозможно слить без потерь.\n\n'
                'Суммарная ширина: {} пикс., при кодировании получится {} пикс.'.format(
                    total_width, actual_total))
            return

        if not messagebox.askyesno(
                'Слияние изображений',
                'Слить изображения {}x{} и {}x{} в одно {}x{}?'.format(
                    width1, height1, width2, height2, total_width, height1)):
            return

        try:
            full_img1 = decode_image(group, idx1, self.palette_row)
            full_img2 = decode_image(group, idx2, self.palette_row)
        except Exception as e:
            log.error('Не удалось декодировать изображения: %s', e)
            messagebox.showerror('Ошибка',
                                 'Не удалось декодировать изображения: {}'.format(e))
            return

        merged_img = Image.new('RGBA', (total_width, height1), (0, 0, 0, 0))
        merged_img.paste(full_img1, (0, 0))
        merged_img.paste(full_img2, (width1, 0))

        try:
            merged_header = img1['header'].copy()
            merged_header['width_words'] = new_width_words
            merged_data = encode_image(merged_img, group, merged_header, self.palette_row)
            merged_header['size'] = 12 + len(merged_data)
        except Exception as e:
            log.error('Не удалось закодировать объединенное изображение: %s', e)
            messagebox.showerror('Ошибка',
                                 'Не удалось закодировать объединенное изображение: {}'.format(e))
            return

        group['images'].pop(idx2)
        group['images'].pop(idx1)
        group['images'].insert(idx1, {'header': merged_header, 'data': merged_data})

        self._rebuild_and_reparse(preserve_entry=True)
        if self.entries:
            self.current_entry = max(0, min(idx1, len(self.entries) - 1))
        self._mark_dirty(True)
        log.info('Изображения слиты: %dx%d + %dx%d → %dx%d',
                 width1, height1, width2, height2, total_width, height1)
        self._set_status('Изображения слиты в одно.')

    def replace_image(self):
        group, ii = self._current()
        if group is None:
            messagebox.showinfo('Информация', 'Сначала откройте TIM-файл.')
            return
        if ii is None or ii < 0 or ii >= len(group['images']):
            messagebox.showinfo('Информация', 'В текущей группе нет изображения.')
            return

        ih = group['images'][ii]['header']
        bpp = group['bpp']
        w = image_width_words_to_pixels(bpp, ih['width_words'])
        h = ih['height']

        path = filedialog.askopenfilename(
            title='Выберите изображение для замены ({}x{})'.format(w, h),
            initialdir=self._initial_dir(),
            filetypes=[('Изображения', '*.png *.bmp'), ('PNG', '*.png'),
                       ('BMP', '*.bmp'), ('Все файлы', '*.*')])
        if not path:
            return
        self._replace_image_from_path(path)

    def _replace_image_from_path(self, path):
        group, ii = self._current()
        if group is None:
            log.warning('Замена изображения: TIM не открыт')
            messagebox.showinfo('Информация', 'Сначала откройте TIM-файл.')
            return
        if ii is None or ii < 0 or ii >= len(group['images']):
            log.warning('Замена изображения: в текущей группе нет изображения')
            messagebox.showinfo('Информация', 'В текущей группе нет изображения.')
            return

        ih = group['images'][ii]['header']
        bpp = group['bpp']
        old_w = image_width_words_to_pixels(bpp, ih['width_words'])
        old_h = ih['height']

        log.info('Замена изображения из %s', os.path.basename(path))

        try:
            img = Image.open(path)
            img.load()
        except Exception as e:
            log.error('Не удалось открыть изображение для замены: %s', e)
            messagebox.showerror('Ошибка', str(e))
            return

        new_w, new_h = img.size
        if new_w < 1 or new_h < 1:
            messagebox.showerror('Ошибка', 'Изображение имеет нулевой размер.')
            return

        new_ww = pixels_to_words(bpp, new_w)
        actual_w = image_width_words_to_pixels(bpp, new_ww)

        size_changed = (actual_w, new_h) != (old_w, old_h)
        pad_needed = new_w != actual_w

        if size_changed or pad_needed:
            lines = ['TIM заголовок изображения будет обновлён по файлу:']
            lines.append('  png файл: {}x{}'.format(new_w, new_h))
            lines.append('  было: width_words={}, height={} ({}x{})'.format(
                ih['width_words'], ih['height'], old_w, old_h))
            lines.append('  станет: width_words={}, height={} ({}x{})'.format(
                new_ww, new_h, actual_w, new_h))
            if pad_needed:
                lines.append('')
                lines.append('Ширина будет округлена до {} пикс. '
                             '({}bpp, word boundary).'.format(
                                 actual_w, bpp * 8 if bpp else 4))
            if not messagebox.askyesno('Замена изображения', '\n'.join(lines)):
                return

        if img.size != (actual_w, new_h):
            img = img.resize((actual_w, new_h), Image.NEAREST)
            log.info('Размер подогнан под word boundary: %dx%d → %dx%d',
                     new_w, new_h, actual_w, new_h)

        try:
            # Обновляем заголовок под новое изображение
            ih['width_words'] = new_ww
            ih['height'] = new_h

            new_data = encode_image(img, group, ih, self.palette_row)
            group['images'][ii]['data'] = new_data
            group['images'][ii]['header']['size'] = 12 + len(new_data)
            self._remember_dir(path)
            self._rebuild_and_reparse(preserve_entry=True)
            self._mark_dirty(True)
            log.info('Изображение заменено: %dx%d → %dx%d (ww=%d, h=%d, %d байт)',
                     old_w, old_h, actual_w, new_h, new_ww, new_h, len(new_data))
            self._set_status('Изображение заменено: {}x{} (было {}x{})'.format(
                actual_w, new_h, old_w, old_h))
        except Exception as e:
            log.error('Ошибка замены изображения: %s', e)
            messagebox.showerror('Ошибка', 'Ошибка замены:\n{}'.format(e))
        
    # ============ Palette I/O ============

    def _any_pal_checked(self):
        return (self.auto_save_pal['pal'].get()
                or self.auto_save_pal['act'].get()
                or self.auto_save_pal['gpl'].get())

    def _auto_save_palettes_for_group(self, group, base_noext, auto_save=None):
        if auto_save is None:
            auto_save = {k: bool(v.get()) for k, v in self.auto_save_pal.items()}
        flat, cw, ch = safe_clut_flat(group)
        if not flat:
            return []
        saved = []
        try:
            if auto_save.get('pal'):
                save_palette_any(base_noext + '.pal', flat, cw, ch)
                saved.append('.pal')
            if auto_save.get('act'):
                save_palette_any(base_noext + '.act', flat, cw, ch)
                saved.append('.act')
            if auto_save.get('gpl'):
                save_palette_any(base_noext + '.gpl', flat, cw, ch)
                saved.append('.gpl')
        except Exception as e:
            log.warning('Не удалось сохранить палитру рядом с PNG: %s', e)
        return saved

    def export_palette(self):
        group, _ = self._current()
        if group is None or not group.get('clut'):
            messagebox.showinfo('Информация', 'Нет палитры.')
            return

        flat, cw, ch = safe_clut_flat(group)
        if not flat:
            messagebox.showinfo('Информация', 'Палитра пуста.')
            return

        stem = self.source_stem or 'palette'
        filetypes = [
            ('Adobe Color Table ACT', '*.act'),
            ('JASC-PAL', '*.pal'),
            ('GIMP GPL', '*.gpl'),
            ('PNG', '*.png'),
            ('BMP', '*.bmp'),
            ('Все файлы', '*.*'),
        ]
        path = filedialog.asksaveasfilename(
            title='Экспорт палитры', initialfile=stem + '_palette',
            initialdir=self._initial_dir(), filetypes=filetypes, defaultextension='')
        if not path:
            return
        base, ext = os.path.splitext(path)
        if not ext:
            path = base + '.png'
        try:
            save_palette_any(path, flat, cw, ch)
            self._remember_dir(path)
            log.info('Экспорт палитры: %s (%d цветов)', path, len(flat))
            self._set_status('Палитра сохранена: {}'.format(path))
        except Exception as e:
            log.error('Не удалось экспортировать палитру: %s', e)
            messagebox.showerror('Ошибка', str(e))

    def _fit_palette(self, palette_flat, cw, ch):
        target = cw * ch
        if target > _MAX_CLUT_ENTRIES:
            log.warning('_fit_palette: cw*ch=%d > %d — обрезаю', target, _MAX_CLUT_ENTRIES)
            target = _MAX_CLUT_ENTRIES

        n = len(palette_flat)
        if n == target:
            return palette_flat
        if n > target:
            if not messagebox.askyesno(
                    'Палитра не подходит',
                    'Импортируемая: {} цветов.\nTIM вмещает: {}.\n\nОбрезать и применить?'.format(
                        n, target)):
                return None
            return palette_flat[:target]
        if not messagebox.askyesno(
                'Палитра не подходит',
                'Импортируемая: {} цветов.\nTIM вмещает: {}.\n\nДополнить чёрным и применить?'.format(
                    n, target)):
            return None
        return palette_flat + [(0, 0, 0)] * (target - n)

    def import_palette(self):
        group, _ = self._current()
        if group is None:
            messagebox.showinfo('Информация', 'Откройте TIM.')
            return
        if not group.get('clut_header'):
            messagebox.showinfo('Информация', 'В TIM нет CLUT.')
            return

        path = filedialog.askopenfilename(
            title='Импорт палитры', initialdir=self._initial_dir(),
            filetypes=[('Палитры', '*.pal *.act *.gpl *.png *.bmp'),
                       ('Adobe Color Table ACT', '*.act'),
                       ('JASC-PAL', '*.pal'),
                       ('GIMP GPL', '*.gpl'),
                       ('Изображения', '*.png *.bmp'),
                       ('Все файлы', '*.*')])
        if not path:
            return
        self._import_palette_from_path(path)

    def _import_palette_from_path(self, path):
        group, _ = self._current()
        if group is None:
            log.warning('Импорт палитры: TIM не открыт')
            messagebox.showinfo('Информация', 'Откройте TIM.')
            return
        if not group.get('clut_header'):
            log.warning('Импорт палитры: в TIM нет CLUT')
            messagebox.showinfo('Информация', 'В TIM нет CLUT.')
            return

        try:
            palette_flat, _ = load_palette_any(path)
        except Exception as e:
            log.error('Не удалось прочитать палитру %s: %s', path, e)
            messagebox.showerror('Ошибка',
                                 'Не удалось прочитать палитру:\n{}'.format(e))
            return

        cw = group['clut_header']['width_words']
        ch = group['clut_header']['height'] or 1
        result = self._fit_palette(palette_flat, cw, ch)
        if result is None:
            log.info('Импорт палитры отменён пользователем')
            return

        out = bytearray()
        for (r, g, b) in result:
            out.extend(struct.pack('<H', rgb_to_bgr555(r, g, b)))

        group['clut'] = bytes(out)
        group['clut_header']['size'] = 12 + len(group['clut'])
        self._rebuild_and_reparse(preserve_entry=True)
        self._mark_dirty(True)
        log.info('Импортирована палитра из %s (%d цветов)',
                 os.path.basename(path), len(result))
        self._set_status('Палитра импортирована из {}'.format(os.path.basename(path)))

    # ============ Batch ============

    def _run_in_thread_with_progress(self, title, worker_fn, done_fn):
        if getattr(self, '_batch_running', False):
            messagebox.showinfo('Информация', 'Пакетная операция уже выполняется.')
            return
        self._batch_running = True

        q = queue.Queue()

        def worker_wrapper():
            try:
                result = worker_fn(q)
                q.put(('__done__', result))
            except Exception as e:
                q.put(('__error__', e, traceback.format_exc()))

        def poll():
            try:
                while True:
                    msg = q.get_nowait()
                    kind = msg[0]
                    if kind == 'status':
                        self._set_status(msg[1])
                    elif kind == 'progress':
                        if len(msg) > 3:
                            self._set_status('{}: {}/{} — {}'.format(
                                title, msg[1], msg[2], msg[3]))
                        else:
                            self._set_status('{}: {}/{}'.format(title, msg[1], msg[2]))
                    elif kind == 'log':
                        log.info(msg[1])
                    elif kind == '__done__':
                        self._batch_running = False
                        try:
                            done_fn(msg[1])
                        except Exception as e:
                            log.exception('Ошибка в done-обработчике: %s', e)
                            messagebox.showerror('Ошибка', str(e))
                        return
                    elif kind == '__error__':
                        self._batch_running = False
                        log.error('Ошибка batch-операции: %s\n%s', msg[1], msg[2])
                        messagebox.showerror('Ошибка', str(msg[1]))
                        self._set_status('Ошибка пакетной операции')
                        return
            except queue.Empty:
                pass
            try:
                if self.root.winfo_exists():
                    self.root.after(80, poll)
            except Exception:
                pass

        threading.Thread(target=worker_wrapper, daemon=True).start()
        self.root.after(80, poll)

    def batch_extract(self):
        folder = filedialog.askdirectory(title='Папка с файлами',
                                         initialdir=self._initial_dir())
        if not folder:
            return
        files = sorted(f for f in os.listdir(folder)
                       if os.path.isfile(os.path.join(folder, f)))
        if not files:
            messagebox.showinfo('Информация', 'Папка пуста.')
            return

        log.info('Пакетное извлечение: папка %s (%d файлов)', folder, len(files))
        auto_save = {k: bool(v.get()) for k, v in self.auto_save_pal.items()}
        any_pal = any(auto_save.values())

        def worker(q):
            count, tim_files, skipped, errors = 0, 0, 0, []
            for idx, fname in enumerate(files):
                q.put(('progress', idx + 1, len(files), fname))
                path = os.path.join(folder, fname)
                try:
                    with open(path, 'rb') as f:
                        data = f.read()
                except Exception as e:
                    errors.append('{}: {}'.format(fname, e))
                    continue

                if data.find(b'\x10\x00\x00\x00') == -1:
                    skipped += 1
                    continue

                try:
                    groups = parse_tims(data)
                except Exception as e:
                    skipped += 1
                    errors.append('{}: {}'.format(fname, e))
                    q.put(('log', 'Пропуск {} (не TIM): {}'.format(fname, e)))
                    continue

                tim_files += 1
                stem = os.path.splitext(fname)[0] or fname
                n = 0
                for g in groups:
                    ws = g.get('warnings', [])
                    for w in ws[:2]:
                        q.put(('log', '{}: {}'.format(fname, w)))
                    if len(ws) > 2:
                        q.put(('log', '{}: ...ещё {} предупреждений'.format(
                            fname, len(ws) - 2)))
                    for i in range(len(g['images'])):
                        n += 1
                        png_path = os.path.join(folder,
                                                '{}_img{:02d}.png'.format(stem, n))
                        try:
                            img = decode_image(g, i, 0)
                            if g['bpp'] == 0:
                                try:
                                    img.save(png_path, bits=4)
                                except Exception:
                                    img.save(png_path)
                            else:
                                img.save(png_path)
                            count += 1
                        except Exception as e:
                            errors.append('{}: {}'.format(png_path, e))
                            continue

                        if any_pal:
                            try:
                                base_noext = os.path.splitext(png_path)[0]
                                self._auto_save_palettes_for_group(
                                    g, base_noext, auto_save)
                            except Exception as e:
                                errors.append('{} (палитра): {}'.format(png_path, e))
            return {'count': count, 'tim_files': tim_files,
                    'skipped': skipped, 'errors': errors}

        def done(result):
            extra_note = ' + палитры' if any_pal else ''
            msg = 'Экспортировано {} PNG{} из {} TIM-файлов (пропущено не-TIM: {}).'.format(
                result['count'], extra_note, result['tim_files'], result['skipped'])
            self._remember_dir(folder + os.sep + 'x')
            log.info('Пакетное извлечение завершено: %d PNG из %d TIM '
                     '(пропущено: %d, ошибок: %d)',
                     result['count'], result['tim_files'],
                     result['skipped'], len(result['errors']))
            messagebox.showinfo('Готово', msg)
            self._set_status('Пакетное извлечение: {} PNG'.format(result['count']))

        self._run_in_thread_with_progress('Извлечение', worker, done)

    def batch_replace(self):
        folder = filedialog.askdirectory(title='Папка с файлами',
                                         initialdir=self._initial_dir())
        if not folder:
            return
        files = sorted(f for f in os.listdir(folder)
                       if os.path.isfile(os.path.join(folder, f)))
        if not files:
            messagebox.showinfo('Информация', 'Папка пуста.')
            return

        if not messagebox.askyesno(
                'Пакетная замена',
                'TIM-файлы в папке будут перезаписаны.\n'
                'PNG ищутся по шаблону <stem>_imgNN.png.\n\nПродолжить?'):
            return

        folder_abs = os.path.normcase(os.path.abspath(folder))
        source_abs = (os.path.normcase(os.path.abspath(self.source_path))
                      if self.source_path else None)

        if self.dirty and source_abs and os.path.dirname(source_abs) == folder_abs:
            if not messagebox.askyesno(
                    'Несохранённые изменения',
                    'Текущий открытый файл находится в этой папке и будет перезаписан.\n\n'
                    'Продолжить?'):
                return

        log.info('Пакетная замена: папка %s (%d файлов)', folder, len(files))

        def worker(q):
            replaced = missing = tim_files = changed_files = skipped = normalized = 0
            errors = []
            opened_file_rewritten = False

            for idx, fname in enumerate(files):
                q.put(('progress', idx + 1, len(files), fname))
                path = os.path.join(folder, fname)
                try:
                    with open(path, 'rb') as f:
                        data = f.read()
                except Exception as e:
                    errors.append('{}: {}'.format(fname, e))
                    continue

                if data.find(b'\x10\x00\x00\x00') == -1:
                    skipped += 1
                    continue

                try:
                    groups = parse_tims(data)
                except Exception as e:
                    skipped += 1
                    errors.append('{}: {}'.format(fname, e))
                    q.put(('log', 'Пропуск {} (не TIM): {}'.format(fname, e)))
                    continue

                tim_files += 1
                stem = os.path.splitext(fname)[0] or fname
                n, changed = 0, False

                for g in groups:
                    for i, img in enumerate(g['images']):
                        n += 1
                        png_name = '{}_img{:02d}.png'.format(stem, n)
                        png_path = os.path.join(folder, png_name)
                        if not os.path.exists(png_path):
                            missing += 1
                            continue
                        try:
                            im = Image.open(png_path)
                            im.load()
                            w = image_width_words_to_pixels(
                                g['bpp'], img['header']['width_words'])
                            h = img['header']['height']
                            if im.size != (w, h):
                                im = im.resize((w, h), Image.NEAREST)
                            new_data = encode_image(im, g, img['header'], 0)
                            img['data'] = new_data
                            img['header']['size'] = 12 + len(new_data)
                            changed = True
                            replaced += 1
                        except Exception as e:
                            errors.append('{} → {}: {}'.format(png_name, fname, e))
                            q.put(('log', 'Ошибка замены {} в {}: {}'.format(
                                png_name, fname, e)))

                if not changed:
                    continue

                try:
                    new_bytes = rebuild_all(groups)
                    old_len = len(data)
                    new_len = len(new_bytes)
                    if new_len != old_len:
                        normalized += 1
                        q.put(('log', '{}: нормализован {} → {} байт (Δ{:+#d})'.format(
                                   fname, old_len, new_len, new_len - old_len)))
                    with open(path, 'wb') as f:
                        f.write(new_bytes)
                    changed_files += 1
                    if source_abs and os.path.normcase(os.path.abspath(path)) == source_abs:
                        opened_file_rewritten = True
                except Exception as e:
                    errors.append('сборка/запись {}: {}'.format(fname, e))
                    q.put(('log', 'Ошибка сборки/записи {}: {}'.format(fname, e)))

            return {
                'replaced': replaced, 'missing': missing,
                'tim_files': tim_files, 'changed_files': changed_files,
                'skipped': skipped, 'errors': errors, 'normalized': normalized,
                'opened_file_rewritten': opened_file_rewritten,
            }

        def done(result):
            if result['opened_file_rewritten'] and self.source_path:
                try:
                    with open(self.source_path, 'rb') as f:
                        data = f.read()
                    self.groups = parse_tims(data)
                    self.entries = make_entries(self.groups)
                    self.current_entry = max(0, min(self.current_entry,
                                                    len(self.entries) - 1))
                    self._refresh_all()
                    self._mark_dirty(False)
                except Exception as e:
                    log.warning('Не удалось перечитать открытый файл: %s', e)

            msg = ('Изменено {} TIM-файлов, заменено {} изображений '
                   '(пропущено не-TIM: {}, не найдено PNG: {}, '
                   '!нормализовано: {}').format(
                result['changed_files'], result['replaced'],
                result['skipped'], result['missing'], result['normalized'])
            if result['errors']:
                msg += '\n\nОшибки (первые 10):\n' + '\n'.join(result['errors'][:10])
            log.info('Пакетная замена: изменено %d TIM, заменено %d изображений '
                     '(всего TIM: %d, пропущено не-TIM: %d, нет PNG: %d, ошибок: %d)',
                     result['changed_files'], result['replaced'], result['tim_files'],
                     result['skipped'], result['missing'], len(result['errors']))
            messagebox.showinfo('Готово', msg)
            self._set_status('Пакетная замена: {} TIM / {} изображений'.format(
                result['changed_files'], result['replaced']))

        self._run_in_thread_with_progress('Замена', worker, done)

    # ============ About ============

    def _about(self):
        text = (
            "{} by pav13(+deepseek), 2026\n"
            "Конвертер TIM <-> PNG для PlayStation 1.\n"
            "\n"
            "Возможности:\n"
            "  * Поддержка 4/8/16/24-битных TIM\n"
            "  * Многострочные CLUT, несколько изображений в группе\n"
            "  * Конвертация 16bpp <-> 24bpp, разделение/слияние изображений\n"
            "  * Палитры: JASC-PAL/ACT/GPL/PNG/BMP\n"
            "  * Автосохранение палитр рядом с PNG\n"
            "  * Пакетные операции (извлечение/замена изображений)\n"
            "  * Открытие сырых TIM без сигнатуры\n"
            "  * Устойчивый парсер (битые заголовки, отступы, обрезанные файлы)\n"
            "  * Тёмная/светлая темы оформления интерфейса\n"
            "  * Drag & Drop:\n"
            "       .PNG/.BMP → замена изображения,\n"
            "       .PAL/.ACT/.GPL → импорт палитры,\n"
            "       остальные файлы → открытие как TIM или «сырого» TIM\n"
            "  * Журнал логов (Вид -> Журнал логов)\n"
            "  * Python 3.4+\n"
            "\n"
            "Оммаж программе TIMViewer 1.04b (rveach, 2004)\n"
            "\n"
            "========== СТРУКТУРА Texture Image Map ==========\n"
            "\n"
            "[1] ГЛОБАЛЬНЫЙ ЗАГОЛОВОК (8 байт)\n"
            "  Байты 0-3:  magic = 0x10 0x00 0x00 0x00\n"
            "  Байты 4-7:  flags (32-битное целое, little-endian)\n"
            "    Биты 0-2: bpp (0=4bit, 1=8bit, 2=16bit, 3=24bit)\n"
            "    Бит 3:    has_clut (1 = есть палитра)\n"
            "    Бит 4:    has_image (1 = есть изображение)\n"
            "    Биты 5-31: зарезервированы (обычно 0)\n"
            "\n"
            "[2] CLUT БЛОК (если has_clut = 1)\n"
            "  Заголовок (12 байт):\n"
            "    size         4 байт  = 12 + размер_данных\n"
            "    x, y         2+2 байт = VRAM координаты палитры\n"
            "    width_words  2 байта  = ширина в 16-битных словах\n"
            "    height       2 байта  = количество строк палитры\n"
            "  Данные (width_words * height * 2 байт):\n"
            "    Формат BGR555: [STP|B4 B3|G4 G3 G2|R4 R3 R2 R1 R0]\n"
            "    STP (бит 15) — флаг полупрозрачности (1 = semi-transparent)\n"
            "    Многострочные CLUT: каждая строка = отдельная палитра\n"
            "\n"
            "[3] IMAGE БЛОКИ (один или несколько, если has_image = 1)\n"
            "  Заголовок (12 байт):\n"
            "    size         4 байта = 12 + размер_данных\n"
            "    x, y         2+2 байта = VRAM координаты изображения\n"
            "    width_words  2 байта   = ширина в 16-битных словах\n"
            "    height       2 байта   = высота в пикселях\n"
            "  Данные (зависят от bpp):\n"
            "    4-bit:  2 пикселя в байте (младший ниббл первый)\n"
            "            ширина в пикселях = width_words * 4\n"
            "            индексы в CLUT (макс. 16 цветов)\n"
            "    8-bit:  1 пиксель в байте\n"
            "            ширина в пикселях = width_words * 2\n"
            "            индексы в CLUT (макс. 256 цветов)\n"
            "    16-bit: 2 байта на пиксель (BGR555 + STP)\n"
            "            ширина в пикселях = width_words\n"
            "            прямой цвет + полупрозрачность\n"
            "    24-bit: 3 байта на пиксель (RGB)\n"
            "            ширина в пикселях = (width_words * 2) / 3\n"
            "            прямой цвет, без прозрачности\n"
            "\n"
            "[4] ДОПОЛНИТЕЛЬНЫЕ ДАННЫЕ\n"
            "  После всех блоков могут следовать:\n"
            "    - Паддинг до выравнивания по 4 байта\n"
            "    - Следующий TIM (сигнатура 0x10 0x00 0x00 0x00)\n"
            "    - Мусор/данные игры (сохраняется как 'extra')\n"
            "\n"
            "=================== VRAM PS1 ===================\n"
            "  Видеопамять PS1: 1024 x 512 пикселей (в 16-битных словах)\n"
            "  Координаты x, y указывают, куда загружать данные в VRAM.\n"
            "  CLUT обычно размещают в нижней части VRAM (y >= 480).\n"
            "\n"
            "=================== ПРИМЕЧАНИЯ ===================\n"
            "  * size всегда включает 12-байтный заголовок блока\n"
            "  * width_words — это 16-битные слова, НЕ пиксели\n"
            "  * 4-bit: 2 пикселя в байте, младший ниббл = первый пиксель\n"
            "  * 16-bit STP=1 означает полупрозрачность (bgra 128)\n"
            "  * В одном файле может быть несколько TIM подряд\n"
            "  * В одной группе может быть несколько image-блоков\n"
        ).format(MAIN_TITLE)

        win = tk.Toplevel(self.root)
        win.title("О программе " + MAIN_TITLE)
        win.transient(self.root)
        win.geometry('640x640')
        win.minsize(640, 640)

        frame = ttk.Frame(win, padding=6)
        frame.pack(fill=tk.BOTH, expand=True)

        txt = tk.Text(frame, wrap=tk.NONE,
                      font=('Consolas', 10) if sys.platform == 'win32' else ('Courier', 11),
                      padx=6, pady=6)
        txt.insert('1.0', text)
        txt.configure(state='disabled')

        sb_y = ttk.Scrollbar(frame, orient='vertical', command=txt.yview)
        sb_x = ttk.Scrollbar(frame, orient='horizontal', command=txt.xview)
        txt.configure(yscrollcommand=sb_y.set, xscrollcommand=sb_x.set)

        txt.grid(row=0, column=0, sticky='nsew')
        sb_y.grid(row=0, column=1, sticky='ns')
        sb_x.grid(row=1, column=0, sticky='we')
        frame.rowconfigure(0, weight=1)
        frame.columnconfigure(0, weight=1)

        def _apply_about_theme(colors):
            try:
                txt.configure(
                    bg=colors['bg2'], fg=colors['fg'],
                    insertbackground=colors['fg'],
                    selectbackground=colors['sel'],
                    selectforeground='#ffffff',
                )
            except Exception:
                pass

        self._register_themed_window(win, _apply_about_theme)
        ttk.Button(win, text='Закрыть', command=win.destroy).pack(
            fill=tk.X, padx=6, pady=(0, 6))


# ---------------------------------------------------------------- entry point

def main():
    log.info('=== Запуск %s ===', MAIN_TITLE)
    try:
        root = TkinterDnD.Tk() if HAS_DND else tk.Tk()
        TTPTT(root)
        root.mainloop()
    finally:
        log.info('=== Завершение работы ===')


if __name__ == '__main__':
    main()