#!/usr/bin/env python3
"""Простой HTTP-сервер с Range-поддержкой и per-connection throttling —
нужен только для бенчмарка (benchmarks/bench_downloader.cpp), не часть
самого download manager'а.

Отдаёт один синтетический файл фиксированного размера и искусственно
ограничивает скорость отдачи на КАЖДОЕ соединение (а не суммарно на весь
сервер) — это моделирует типичный сценарий, из-за которого параллельные
закачки вообще имеют смысл: один реальный сервер/CDN часто режет скорость
одного TCP-соединения, но параллельные соединения складываются.

Запуск: python3 throttled_server.py [port]
Переменные окружения:
  DLM_BENCH_FILE_SIZE  — размер файла в байтах (по умолчанию 20 MiB)
  DLM_BENCH_RATE_BPS   — лимit на одно соединение, байт/сек (по умолчанию 1 MiB/s)
"""
import http.server
import os
import re
import socketserver
import sys
import time

FILE_SIZE = int(os.environ.get("DLM_BENCH_FILE_SIZE", 20 * 1024 * 1024))
RATE_BPS = int(os.environ.get("DLM_BENCH_RATE_BPS", 1024 * 1024))
WRITE_CHUNK = 32 * 1024


class ThrottledHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        total = FILE_SIZE
        range_header = self.headers.get("Range")
        if range_header:
            match = re.match(r"bytes=(\d+)-(\d*)", range_header)
            if not match:
                self.send_error(416, "Invalid Range")
                return
            start = int(match.group(1))
            end = int(match.group(2)) if match.group(2) else total - 1
            end = min(end, total - 1)
            if start > end or start >= total:
                self.send_error(416, "Range Not Satisfiable")
                return
            length = end - start + 1
            self.send_response(206)
            self.send_header("Content-Range", f"bytes {start}-{end}/{total}")
            self.send_header("Content-Length", str(length))
            self.send_header("Accept-Ranges", "bytes")
            self.send_header("Connection", "close")
            self.end_headers()
            self._stream(length)
        else:
            self.send_response(200)
            self.send_header("Content-Length", str(total))
            self.send_header("Accept-Ranges", "bytes")
            self.send_header("Connection", "close")
            self.end_headers()
            self._stream(total)

    def _stream(self, length):
        remaining = length
        sent = 0
        start_time = time.monotonic()
        buf = b"\0" * WRITE_CHUNK
        try:
            while remaining > 0:
                n = min(WRITE_CHUNK, remaining)
                self.wfile.write(buf[:n] if n < WRITE_CHUNK else buf)
                remaining -= n
                sent += n
                expected = sent / RATE_BPS
                actual = time.monotonic() - start_time
                if expected > actual:
                    time.sleep(expected - actual)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def log_message(self, fmt, *args):
        pass


class ThreadingServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8787
    server = ThreadingServer(("127.0.0.1", port), ThrottledHandler)
    print(f"throttled_server: port={port} file_size={FILE_SIZE} rate_bps_per_conn={RATE_BPS}", flush=True)
    server.serve_forever()
