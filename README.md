# LocalLens

**A local-first document intelligence & search engine.** LocalLens watches folders you
choose, indexes every text/markdown/PDF document inside them, and gives you instant
full-text search with relevance ranking — completely offline. No cloud, no telemetry,
your files never leave your machine.

Built with **C++20** and **Qt 6 (QML)**.

## Features

- **Instant search** with BM25 relevance ranking and microsecond-level query timing
- **Type-ahead autocomplete** driven by the live term index
- **Live folder watching** — native OS filesystem events (`ReadDirectoryChangesW` on
  Windows) trigger re-indexing the moment a file changes, no polling
- **Embedded SQLite inverted index** — a single portable database file
- **Metrics dashboard** — indexed document count, indexing queue depth, database size,
  and per-query execution time, updating live

## Architecture

LocalLens uses a strict multi-threaded MVC design so the UI thread never blocks:

```
┌────────────────────────────────────────────────────────────┐
│  UI Thread (main)                                          │
│  QML dashboard ── AppController ── result/folder models    │
└───────▲──────────────────────────────────────┬─────────────┘
        │ queued signals (results, metrics)    │ tasks / queries
┌───────┴──────────────┐            ┌──────────▼─────────────┐
│  Watcher Thread(s)   │  events    │  Indexer Worker Thread │
│  FilesystemWatcher   ├───────────▶│  TaskQueue (mutex+cv)  │
│  ReadDirectoryChangesW│           │  extract → tokenize    │
└──────────────────────┘            │  → update index        │
                                    └──────────┬─────────────┘
                                    ┌──────────▼─────────────┐
                                    │  SQLite (WAL mode)     │
                                    │  documents / terms /   │
                                    │  postings (inverted    │
                                    │  index + term freq)    │
                                    └────────────────────────┘
```

- **UI thread** runs the Qt/QML event loop. All cross-thread communication uses Qt
  queued signals/slots — no shared mutable state with the UI.
- **Watcher thread** wraps the native OS change-notification API behind a portable
  `FilesystemWatcher` interface and converts raw events into index tasks.
- **Indexer worker thread** drains a condition-variable-based `TaskQueue`, reads file
  contents, tokenizes, and writes the inverted index inside SQLite transactions.
- **Search** runs on the Qt thread pool with its own read-only DB connection, so
  queries never wait behind indexing (SQLite WAL allows concurrent readers).

### Project layout

```
src/
├── core/       tokenizer, text extraction, task queue, indexer worker, BM25 search
├── database/   SQLite-backed inverted index (IndexStore)
├── watcher/    FilesystemWatcher interface + per-OS backends
└── ui/         QML dashboard + C++ controllers/models
tests/          unit tests (Qt Test / ctest)
```

## Building

Requires CMake ≥ 3.21, a C++20 compiler, and Qt 6.4+ (Core, Gui, Qml, Quick,
QuickControls2, Sql).

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build        # run unit tests
./build/locallens             # launch
```

On Windows with MSYS2/UCRT64 Qt, pass `-DCMAKE_PREFIX_PATH=C:/msys64/ucrt64`.

## Roadmap

- [x] Project scaffold & CMake build
- [ ] Tokenization pipeline
- [ ] SQLite inverted index with term frequencies
- [ ] Indexer worker thread + thread-safe task queue
- [ ] BM25 ranked search
- [ ] Native filesystem watcher (Win32)
- [ ] QML dashboard: search, results, live metrics, folder management
- [ ] PDF text extraction
