# Multithreaded Download Manager (dlm)

[![CI](https://github.com/MAX959954/download_manager/actions/workflows/ci.yml/badge.svg)](https://github.com/MAX959954/download_manager/actions/workflows/ci.yml)

Многопоточный менеджер загрузок на C++: параллельная докачка файлов через
HTTP Range-запросы, пул потоков, возобновление после обрыва, ограничение
скорости и Qt-интерфейс.

## О проекте

Multithreaded Download Manager — десктопный менеджер загрузок на C++17/Qt,
который скачивает файл в несколько параллельных соединений, разбивая его на
чанки и запрашивая каждый через `Range: bytes=`.

### Ключевые возможности

- **Параллельная загрузка** — собственный пул потоков (`std::thread` +
  `condition_variable`); N воркеров независимо качают чанки
  `[offset, offset+size)` одного файла.
- **Пауза и докачка** — состояние сохраняется в метафайл рядом с загрузкой
  (карта готовых чанков, ETag/Last-Modified), загрузка продолжается после
  перезапуска приложения или обрыва сети.
- **Прямая запись по смещениям** — каждый воркер пишет в свой регион файла
  (seek/pwrite), без временных частей и финальной склейки.
- **Управление скоростью** — ограничение пропускной способности через token
  bucket, общий лимит и лимит на загрузку.
- **Контроль целостности** — проверка контрольной суммы (SHA-256) после
  завершения.
- **Отмена операций** — кооперативная остановка через `std::atomic` /
  stop-token на любом этапе.
- **Qt GUI** — очередь заданий, прогресс по каждому чанку, текущая скорость,
  пауза/возобновление/отмена.
- HTTP-слой абстрагирован за интерфейсом `IHttpClient`: реализация на
  libcurl и (в перспективе) альтернативный клиент на сокетах + OpenSSL.

## Сборка

Требуется [vcpkg](https://github.com/microsoft/vcpkg) (переменная окружения
`VCPKG_ROOT`), CMake ≥ 3.21 и Ninja.

```bash
cmake --preset default
cmake --build build/default
```

Собранный CLI:

```bash
./build/default/download_manager <url> -o <file>
```

## CI и проверка потокобезопасности

GitHub Actions на каждый push/PR:

- **build & test** — обычная Debug-сборка (Linux, системные `libcurl`/`openssl`),
  прогон всех офлайн-тестов через `ctest`.
- **sanitizers** — те же тесты, собранные и прогнанные под
  **ThreadSanitizer** и **AddressSanitizer + UndefinedBehaviorSanitizer**.
  `Downloader`/`DownloadManager` гоняют `curl`/сокеты внутри нескольких
  `std::thread`, разделяя `FileWriter`, метафайл и атомарные флаги паузы —
  TSan здесь реально ловит гонки, если они появятся, а не просто
  «зелёная галочка для вида».
- **live network tests** — отдельный необязательный джоб: три теста ходят
  по-настоящему в интернет (TLS-рукопожатие и Range-запросы к
  raw.githubusercontent.com) и проверяют `SocketHttpClient`/`CurlHttpClient`
  на живом сервере с проверкой SHA-256. Вынесены отдельно и помечены
  `continue-on-error`, чтобы временные сетевые проблемы раннера не красили
  основную сборку.

Локально то же самое:

```bash
cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -g"
cmake --build build-tsan
ctest --test-dir build-tsan --output-on-failure -LE live
```

## Дорожная карта

- [x] **Этап 0. Каркас** — CMake-проект (`core` / `cli` / позже `gui`),
      зависимости через vcpkg, `dlm_core` слинкован с libcurl.
- [x] **Этап 1. Одно соединение, один файл** — `IHttpClient` / `CurlHttpClient`
      поверх libcurl easy handle (редиректы, `Content-Length`,
      `Accept-Ranges`, `ETag`/`Last-Modified`), `Downloader::downloadToFile`
      пишет тело ответа в файл потоком. `dlm <url> -o file` работает.
- [ ] **Этап 2. Чанки, но последовательно** — разбиение на чанки
      фиксированного размера, преаллокация файла, запись по offset.
- [ ] **Этап 3. Thread pool** — свой пул воркеров, каждый со своим CURL easy
      handle.
- [ ] **Этап 4. Докачка** — метафайл `file.dlm`, `If-Range`, битовая карта
      готовых чанков.
- [ ] **Этап 5. Пауза/отмена** — кооперативная остановка через
      `CURLOPT_XFERINFOFUNCTION`.
- [ ] **Этап 6. Менеджер загрузок** — очередь заданий, публичный API для GUI.
- [ ] **Этап 7. Qt GUI** — тонкий слой поверх API движка.
- [ ] **Этап 8. «Вау»** — token bucket, SHA-256, ретраи с backoff, свой
      HTTP+TLS клиент.

### Решения, принятые заранее

| Вопрос | Решение |
|---|---|
| Модель чанков | Фиксированный размер + очередь, не N равных частей |
| Запись | Преаллокация + запись по offset, без временных частей и склейки |
| Пул | Один глобальный, задача = чанк; per-download только счётчики/флаги |
| HTTP | libcurl easy handle на поток; свой клиент — в конце |
| Валидация докачки | Хранить и слать `ETag`/`Last-Modified` через `If-Range` |
| Fallback | Нет `Accept-Ranges` → одно соединение, без чанков |
