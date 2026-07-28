# NoBugDB

The world's first database with absolutely no bugs.

<small>
There are no bugs.

Only undocumented features.
</small>

**NoBugDB** (`nobugdb`) is a modular **C++17** SQL engine with a TCP server: parsing a subset of SQL, in-memory table operations, binary on-disk persistence, and client access via a simple text protocol.

---

## Table of contents

- [Features](#features)
- [Stack and dependencies](#stack-and-dependencies)
- [Project structure](#project-structure)
- [Build](#build)
- [Testing](#testing)
- [Running](#running)
- [Troubleshooting](#troubleshooting)
- [Network protocol](#network-protocol)
- [Persistence](#persistence)
- [Backup and restore](#backup-and-restore)
- [Supported SQL](#supported-sql)
- [Architecture](#architecture)
- [Concurrency and performance](#concurrency-and-performance)
- [SQL examples](#sql-examples)
- [Logging and errors](#logging-and-errors)
- [Limitations and future work](#limitations-and-future-work)
- [Extending the codebase](#extending-the-codebase)
- [Statistics (approximate)](#statistics-approximate)
- [License and references](#license-and-references)

---

## Features

### Fully supported by the executor

- **CREATE TABLE** — schema with types and `PRIMARY KEY`, `UNIQUE`, `NOT NULL`, and table/column `CHECK (expression)` constraints. Declarative **partitioning**: `PARTITION BY RANGE (col)` / `PARTITION BY HASH (col)` on the parent; children via `CREATE TABLE child PARTITION OF parent FOR VALUES FROM (a) TO (b)` or `WITH (MODULUS n, REMAINDER r)`.
- **CREATE VIEW** / **DROP VIEW** — named SELECT definitions; `SELECT`/`JOIN` against a view materializes its defining query (non-updatable in v1).
- **CREATE FUNCTION** / **DROP FUNCTION** — SQL-bodied scalar UDF: `CREATE FUNCTION name(params) RETURNS type AS $$ RETURN expr; $$` or short `AS (expr)` / `AS expr`. Usable in SELECT expressions; persisted under `data/_routines/*.func`.
- **CREATE PROCEDURE** / **DROP PROCEDURE** / **CALL** — body is `AS $$ stmt; ... $$` only; `CALL name(args)` runs statements in order with IN params as locals. Persisted under `data/_routines/*.proc`.
- **CREATE TRIGGER** / **DROP TRIGGER** — row-level `BEFORE|AFTER INSERT|UPDATE|DELETE` with `FOR EACH ROW EXECUTE $$ ... $$`. Body may use `NEW`/`OLD` column refs, `SET NEW.col = expr` (BEFORE INSERT/UPDATE), and `CALL`. Recursion depth capped at 16. Persisted under `data/_triggers/*.trig`.
- **DROP TABLE** — removes the table from memory and deletes its `.db` file. Dropping a partitioned parent cascades to all children; dropping a child unlinks that partition.
- **ALTER TABLE** — `ADD`/`DROP COLUMN`, `RENAME TO` / `RENAME COLUMN`, `ADD`/`DROP PRIMARY KEY`, `ADD`/`DROP UNIQUE (col)`, `ALTER COLUMN ... SET|DROP NOT NULL`, `ADD`/`DROP CHECK`.
- **INSERT** — one or many rows; explicit column list or table column order; **VALUES** accept expressions (literals, arithmetic, procedure/UDF locals). Into a partitioned parent, rows are routed to the matching child (error if no partition matches).
- **UPDATE** / **DELETE** — optional **WHERE**. On partitioned parents, prune children and relocate rows when the partition key changes. **`UPDATE` `SET`** accepts only literals and column references on the right-hand side (no arithmetic).
- **SELECT** — column list or `*`, optional **WHERE** (including **BETWEEN**), expressions in the SELECT list (arithmetic, qualified or unambiguous column references), **DISTINCT**, **GROUP BY**, **HAVING**, **ORDER BY** (ASC/DESC), **LIMIT**, **OFFSET**. Top-level queries may combine SELECT bodies with **UNION** / **UNION ALL** / **INTERSECT** / **EXCEPT** (same column count; exact `DataType` match; optional parentheses for nesting; **ORDER BY** / **LIMIT** / **OFFSET** only on the outermost set-op). **FROM** may name a single table **or** chain several tables with **JOIN**: **INNER** (including bare `JOIN`), **LEFT** / **RIGHT** / **FULL** with optional **OUTER**, and **CROSS**. **ON** is required for **LEFT**, **RIGHT**, and **FULL**; optional for **INNER** (without **ON**, the join is a Cartesian product); forbidden for **CROSS** (parse error if **ON** is present). With multiple tables, [`JoinSelectExecutor`](src/executor/join_select_executor.cc) runs joins: for two-table **INNER** equi-joins, [`JoinMethodChooser`](include/planner/join_method_chooser.h) picks **HashJoin**, **IndexNestedLoop**, or **NestedLoop** using [`TableStatistics`](include/planner/table_statistics.h) (NDV / equal-width histograms, refreshed on `VACUUM` or lazily); hash build uses [`HashJoinExecutor`](include/executor/hash_join_executor.h). Outer joins and non-equi still use nested-loop (equi **LEFT** may B-tree-probe the right side when indexed). `SELECT *` emits **qualified** headers (`alias.column`). Bare column names must be unique across participating tables or execution reports an ambiguity error. Single-table queries still use [`SelectExecutor`](src/executor/query_executor.cc). After scan/join, [`SelectPipeline`](include/executor/select_pipeline.h) applies relational operators: [`GroupAggregateOperator`](include/executor/group_aggregate_operator.h) (grouping + aggregates + HAVING), [`DistinctOperator`](include/executor/distinct_operator.h), [`SortOperator`](include/executor/sort_operator.h), [`LimitOffsetOperator`](include/executor/limit_offset_operator.h). Set algebra uses [`SetOperationOperator`](include/executor/set_operation_operator.h). Expression evaluation shared via [`SelectExpressionEvaluator`](include/executor/select_expression_evaluator.h).
- **EXPLAIN** — prefix any statement (`EXPLAIN SELECT ...`); the command is executed and a textual plan is returned in column `QUERY PLAN` (access path, join order, join method, and `PartitionPrune` for partitioned scans).
- **Indexes** — in-memory B-trees on **PRIMARY KEY** / **UNIQUE** columns; used for equality and range predicates in WHERE / UPDATE / DELETE and for INNER/LEFT equi-JOIN probes. Indexes are per child heap for partitioned tables.
- Aggregate functions: **COUNT** (`*` or expression), **SUM**, **AVG**, **MIN**, **MAX** with **GROUP BY** or implicit single-group queries (`SELECT COUNT(*) FROM t`). Scalar builtins (via [`ScalarFunctionRegistry`](include/executor/scalar_function.h)): **UPPER**, **LOWER**, **LENGTH**, **COALESCE**, **NULLIF**, **SUBSTRING**/**SUBSTR**, **CURRENT_DATE**; **CAST(expr AS type)** as a dedicated AST node. Unknown function names fail with an error (not silent NULL). User scalar functions from `CREATE FUNCTION` resolve after builtins.
- **Window functions** — `ROW_NUMBER()`, `RANK()`, `DENSE_RANK()`, running `SUM(expr)` / `AVG(expr)` with `OVER (PARTITION BY ... ORDER BY ...)`. Frame for running aggregates is **ROWS** (UNBOUNDED PRECEDING … CURRENT ROW). `ORDER BY` in `OVER` is required in v1. Columns referenced in `OVER` must appear in the SELECT list (resolved against projected result names). Implemented by [`WindowOperator`](include/executor/window_operator.h) after grouping, before DISTINCT.
- **Types**: `INT`, `FLOAT`, `STRING`, `BOOLEAN`, `DATE`, `UUID` and synonyms from [`data_type.h`](include/types/data_type.h) (`INTEGER`, `REAL`, `TEXT`, `VARCHAR`, `BOOL`).
- **Persistence**: atomic per-table saves (write `.tmp` then rename); only dirty tables are flushed after mutations; **page-oriented heap** (8 KiB) with a configurable **buffer pool**. Partition metadata: `{data_dir}/_partitions/{parent}.part`. Offline **backup/restore** via `bin/db_backup` / `make backup` (SHA-256 manifest).
- **Builtin sharding (v1)** — coordinator + workers with a static `shard_map.conf` (child partition → shard). Single-shard DML/SELECT is proxied; multi-shard SELECT uses scatter-gather. Flags: `--role=coordinator|worker`, `--shard-map`, `--rpc-secret`. Demo: `make demo-shard`.
- **Network**: multi-client TCP server; **AUTH**, **QUERY**, **PING**, **QUIT**, internode **RPC_QUERY**; optional file-based auth with `admin` / `reader` roles.
- **Client**: interactive mode and batch execution from a `.sql` file; optional `--user` / `--password` for AUTH.

### SELECT limitations (v1)

- **GROUP BY** validation: non-aggregate SELECT columns must match a **GROUP BY** expression; `SELECT *` is rejected with **GROUP BY** or bare aggregates.
- Aggregates run with **GROUP BY** or a single implicit group; windowed `SUM`/`AVG` do not require grouping.
- No `LEAD` / `LAG` / `NTILE`, named `WINDOW` clauses, or explicit `ROWS BETWEEN` / `RANGE` frame clauses.
- **ORDER BY** column position (`ORDER BY 1`) is not supported; use column names or expressions.
- **INTERSECT ALL** / **EXCEPT ALL** and **CORRESPONDING** are not supported; set ops require exact column `DataType` match.
- Set ops in view definitions and subqueries are not supported (top-level queries only).
- **UPDATE** `SET` still evaluates only **literals** and **column references** on the right-hand side (no arithmetic in `SET`), via [`SelectExpressionEvaluator::evaluate_dml_assignment_rhs`](include/executor/select_expression_evaluator.h).
- Routines: no tagged dollar-quotes (`$tag$`), no OUT/INOUT, no table-valued UDF, no PL/native plugins, no nested TX-`BEGIN` inside a procedure body. CREATE/DROP FUNCTION|PROCEDURE require `admin`; CALL is denied for `reader` in v1.
- Triggers: no statement-level / `INSTEAD OF` / `WHEN` clause; CREATE/DROP TRIGGER require `admin`. BEFORE sees pending NEW; AFTER sees the row after the mutation in the same TX.
- Table partitioning v1: no SUBPARTITION, no global indexes, no FK on partitioned parents, no ATTACH/DETACH beyond create/drop partition.
- Builtin sharding v1: static map only; no distributed transactions, no cross-shard JOIN, no auto-rebalance / Raft metadata.

### C++ API

- **`Database::drop_table`** / SQL **`DROP TABLE`** both remove the table from memory and delete its persisted `.db` file.

---

## Stack and dependencies

| Component | Details |
|-----------|---------|
| Language | C++17 |
| Build | [Makefile](Makefile), `g++` |
| Platform | **Linux**, **macOS** (POSIX backend), **Windows** (MinGW-w64 / MSYS2, Winsock backend)—see [Build](#build) |
| Dependencies (server & client) | C++ standard library only for `bin/nobugdb` and `bin/nobugdb-cli` |
| Dependencies (tests) | Optional: [GoogleTest](https://github.com/google/googletest) via git submodule [`third_party/googletest`](third_party/googletest)—run `git submodule update --init --recursive` before `make test` |

Server entry point: [`main.cc`](main.cc). Defaults: port **9000**, workers **4**, data directory **`data`**, log level **INFO**. Override via CLI flags (see [Running](#running)).

---

## Project structure

```
nobugdb/               # repository root
├── include/
│   ├── core/          # Database, Table, Row, Column
│   ├── parser/        # Lexer, Parser, AST, Token
│   ├── executor/      # QueryExecutor, relational operators, SelectPipeline
│   ├── storage/       # PersistenceManager, BufferPool, HeapFile, BackupService, pages
│   ├── threading/     # ThreadPool, WorkQueue
│   ├── network/       # Server, Connection, Protocol
│   ├── platform/      # TcpSocket, ShutdownHandler, process helpers (OS abstraction)
│   ├── types/         # Value, DataType, TypeConverter
│   └── utils/         # Logger, Exceptions, SHA-256, engine version
├── src/               # Implementations (.cc), mirrors include/
│   └── platform/      # posix/ and win32/ backends + shared tcp_client.cc
├── tests/             # GoogleTest unit and integration tests
├── third_party/
│   └── googletest/    # git submodule (for `make test`)
├── client/
│   └── cli_client.cc  # TCP client
├── tools/
│   └── db_backup.cc   # Offline backup / restore / checkpoint CLI
├── data/              # Table files (created at runtime)
├── main.cc
├── Makefile
└── README.md
```

---

## Build

### `make` targets

| Target | Action |
|--------|--------|
| `make` / `make all` / `make build` | Build server → `bin/nobugdb` |
| `make server` | Same, with a success message |
| `make client` | Build client → `bin/nobugdb-cli` |
| `make backup-tool` | Build offline backup CLI → `bin/db_backup` |
| `make backup` | Offline backup of `DATA_DIR` (default `data/`) into `BACKUP_DIR` (default `backup/`); **stop the server first** |
| `make restore` | Restore `BACKUP_DIR` into `DATA_DIR` with `--force` |
| `make run` | Build and run the server |
| `make client-run` | Build and run the client (interactive) |
| `make demo` | Server in background, runs temporary SQL from `build/demo_queries.sql`, then stops server |
| `make tests` | Build test binary → `bin/run_tests` (requires initialized `third_party/googletest` submodule) |
| `make test` | Build and run `bin/run_tests` |
| `make clean` | Remove `build/` and `bin/` |
| `make data-clean` | Remove `data/` |
| `make distclean` | `clean` + `data-clean` |
| `make help` | Short help for targets |

Compiler flags: `-std=c++17 -Wall -Wextra -O2 -Iinclude`. The [Makefile](Makefile) selects the platform backend automatically (`posix` on Linux/macOS, `win32` when `OS=Windows_NT`).

### Supported platforms

| OS | Toolchain | Link flags | Notes |
|----|-----------|------------|-------|
| **Linux** | `g++`, `make` | `-pthread` | Primary CI/dev target |
| **macOS** | Xcode CLT or Homebrew `g++`, `make` | `-pthread` | Same POSIX backend as Linux |
| **Windows** | MSYS2 MinGW-w64 (`mingw-w64-x86_64-gcc`, `make`) | `-lws2_32` | Build from **MSYS2 MinGW** shell, not MSVC |

**Linux / macOS:**

```bash
make build
make client
make test
```

**Windows (MSYS2 MinGW 64-bit):**

```bash
pacman -S --needed mingw-w64-x86_64-gcc make
make build
make client
make test
```

`make help` prints the active backend (`posix` or `win32`). Test objects use extra include paths for GoogleTest.

---

## Testing

One-time setup if the submodule is empty:

```bash
git submodule update --init --recursive
```

Then:

```bash
make test
```

This compiles and runs `bin/run_tests`, which links the full server object set with GoogleTest. Coverage is organized by file under [`tests/`](tests/):

| Test source | Focus (high level) |
|-------------|--------------------|
| [`lexer_test.cc`](tests/lexer_test.cc) | Lexer tokens and edge cases; join keywords **FULL**, **OUTER**, **CROSS** |
| [`parser_test.cc`](tests/parser_test.cc) | Parser ↔ SQL statements |
| [`parser_ast_advanced_test.cc`](tests/parser_ast_advanced_test.cc) | Richer AST / parser scenarios; **CROSS** / **FULL OUTER** / **LEFT OUTER** join parsing |
| [`value_test.cc`](tests/value_test.cc) | `Value` type behavior |
| [`type_converter_test.cc`](tests/type_converter_test.cc) | Type conversion |
| [`row_test.cc`](tests/row_test.cc) | Row layout and access |
| [`ddl_test.cc`](tests/ddl_test.cc) | **DROP TABLE**, **ALTER TABLE**, **BETWEEN** parse |
| [`check_constraint_test.cc`](tests/check_constraint_test.cc) | **CHECK** on CREATE/ALTER, INSERT/UPDATE enforcement, NULL semantics, persist v5 |
| [`view_test.cc`](tests/view_test.cc) | **CREATE**/**DROP VIEW**, SELECT from view, JOIN views, name clash, nesting/cycles, persist reload, EXPLAIN `ViewScan` |
| [`builtin_function_test.cc`](tests/builtin_function_test.cc) | **COALESCE**, **NULLIF**, **CAST**, **SUBSTRING**, **CURRENT_DATE**, unknown function error |
| [`udf_test.cc`](tests/udf_test.cc) | **CREATE**/**DROP FUNCTION** (`AS $$…$$` and short `AS`), SELECT use, persist reload, unclosed `$$` |
| [`procedure_test.cc`](tests/procedure_test.cc) | **CREATE**/**DROP PROCEDURE**, **CALL**, multi-stmt stop on error, persist reload |
| [`trigger_test.cc`](tests/trigger_test.cc) | **CREATE**/**DROP TRIGGER**, AFTER audit, BEFORE `SET NEW`, recursion limit, ROLLBACK, persist reload |
| [`persistence_test.cc`](tests/persistence_test.cc) | Atomic save, dirty-only flush, drop/rename files |
| [`backup_test.cc`](tests/backup_test.cc) | Offline backup/restore, manifest checksums, staging refuse |
| [`btree_index_test.cc`](tests/btree_index_test.cc) | B-tree lookups, indexed SELECT / JOIN |
| [`cli_options_test.cc`](tests/cli_options_test.cc) | Server CLI flag parsing |
| [`database_test.cc`](tests/database_test.cc), [`database_extended_test.cc`](tests/database_extended_test.cc) | Database operations and persistence-oriented checks |
| [`select_join_test.cc`](tests/select_join_test.cc) | INNER / LEFT / RIGHT / FULL OUTER / CROSS joins, cartesian INNER without **ON**, chaining, parse errors, ambiguity, wildcard headers |
| [`select_relational_ops_test.cc`](tests/select_relational_ops_test.cc) | **GROUP BY**, **HAVING**, **ORDER BY**, **LIMIT**/**OFFSET**, **COUNT**/**SUM**/**AVG**/**MIN**/**MAX**, join + grouping |
| [`set_operation_test.cc`](tests/set_operation_test.cc) | **UNION** / **UNION ALL** / **INTERSECT** / **EXCEPT**, arity/type errors, ORDER BY after set-op, EXPLAIN |
| [`window_function_test.cc`](tests/window_function_test.cc) | **ROW_NUMBER** / **RANK** / **DENSE_RANK** / running **SUM**/**AVG**, ORDER BY required, EXPLAIN `Window` |
| [`partition_test.cc`](tests/partition_test.cc) | **PARTITION BY RANGE/HASH**, insert routing, prune EXPLAIN, persist reload, DROP partition/parent cascade |
| [`shard_router_test.cc`](tests/shard_router_test.cc) | Shard map parse, key → shard, prune → endpoints |
| [`coordinator_merge_test.cc`](tests/coordinator_merge_test.cc) | Scatter merge, proxy INSERT/SELECT, multi-shard TX / cross-shard JOIN reject |
| [`planner_test.cc`](tests/planner_test.cc) | Access path and join-order planner unit tests |
| [`explain_test.cc`](tests/explain_test.cc) | **EXPLAIN** parse, plan output, and side-effect execution |
| [`platform_tcp_socket_test.cc`](tests/platform_tcp_socket_test.cc) | Platform `TcpSocket` loopback send/recv |
| [`network_integration_test.cc`](tests/network_integration_test.cc) | TCP server + client-style exchange via [`tcp_exchange`](include/platform/tcp_client.h) |

Shared helpers live in [`tests/test_util.hh`](tests/test_util.hh).

---

## Running

**Terminal 1 — server:**

```bash
make run
# or:
./bin/nobugdb --port 9000 --workers 4 --data-dir data --log-level INFO
# flags: -p/--port, -w/--workers, -d/--data-dir, --log-level,
#         --auth-file, --require-auth, --no-require-auth, --bootstrap-admin,
#         --role, --shard-id, --shard-map, --rpc-secret, -h/--help
# listens on all interfaces (INADDR_ANY)
# stop: Ctrl+C (POSIX) or Ctrl+C / Ctrl+Break (Windows)
```

**Sharded cluster (2 workers + coordinator):**

```bash
make demo-shard
# or manually: start workers with --role=worker --rpc-secret=... --shard-id=N
# then coordinator with --role=coordinator --shard-map=shard_map.conf --rpc-secret=...
# Backup each worker data-dir before changing the shard map.
```

**Optional authentication:**

```bash
# Create users.conf with an admin account, then require AUTH before QUERY
./bin/nobugdb --data-dir data --auth-file data/users.conf --bootstrap-admin 'secret'

# Client with credentials (password may also come from NOBUGDB_PASSWORD)
./bin/nobugdb-cli --user admin --password secret
./bin/nobugdb-cli -u admin --password secret path/to/queries.sql
```

Users file format (`username:role:salt_hex:hash_hex`):

```
# role is admin or reader
# hash = SHA-256(salt_bytes || password_utf8) as hex; salt is 16 bytes
admin:admin:<32_hex_chars_salt>:<64_hex_chars_hash>
viewer:reader:<salt>:<hash>
```

- **`admin`**: all SQL.
- **`reader`**: SELECT, EXPLAIN of allowed statements, BEGIN/COMMIT/ROLLBACK, PREPARE/EXECUTE of read-only SQL; writers and DDL denied (including CREATE/DROP FUNCTION|PROCEDURE and CALL).
- Without `--auth-file`, auth is off (open QUERY, as before). With `--auth-file`, `--require-auth` defaults to on unless `--no-require-auth`.
- Passwords are never logged (AUTH payloads are redacted). This is a simple teaching/demo auth layer — **not production-hardened** (no TLS, no rate limiting, SHA-256 of salt+password only).

**Terminal 2 — client:**

```bash
make client-run
```

Batch mode:

```bash
./bin/nobugdb-cli path/to/queries.sql
```

Lines starting with `#` and empty lines are ignored. A statement is assembled until a line ends with `;`.

---

## Troubleshooting

| Symptom | Likely cause |
|---------|----------------|
| `Failed to bind socket` | Port in use or previous server still holding it—pass `--port` / `-p` or free the port (`SO_REUSEADDR` is already enabled). |
| Client: `Connection failed` | Server not running, or wrong host/port in `cli_client.cc` (defaults: `127.0.0.1:9000`). |
| Empty or truncated response | Query or result larger than the receive buffer—see [Network protocol](#network-protocol). |
| Data missing after `Ctrl+C` | Saves run after successful **INSERT**/**UPDATE**/**DELETE**/**CREATE TABLE**; abrupt termination may leave files at the last successful save only. |
| Compile errors | Need a **C++17** compiler (`g++` 8+). On Windows use the **MinGW** MSYS2 environment, not plain `cmd` without `g++`. |
| Windows: link errors mentioning `socket` / `WSAStartup` | Build with the project Makefile (adds `-lws2_32`); ensure `TcpSocket::startup()` ran (server/client call it on start). |
| Windows: `make demo` fails | Demo uses shell-specific process control; run server and client manually in two terminals if needed. |

---

## Network protocol

Client messages are UTF-8 lines terminated by `\n`. Server-side parsing: [`Protocol::parse_request`](include/network/protocol.h).

### Requests

| Kind | Format | Action |
|------|--------|--------|
| Auth | **AUTH** + `\|` + username + `\|` + password + `\n` | Verify credentials; response `OK|authenticated\n` or `ERROR|...` |
| SQL | Line built like the CLI: protocol verb **QUERY**, a delimiter ASCII `\|` (U+007C), the SQL text, then LF `\n` | Run SQL via [`Database::execute_query`](src/core/database.cc) (requires prior AUTH when `--require-auth`) |
| Health check | **PING** + `\|` + payload + `\n` — delimiter required (`Protocol::parse_request`) | Response `PONG\n` (allowed without AUTH) |
| Quit | **QUIT** + `\|` + payload + `\n` | Response `OK|Goodbye\n`, session ends (allowed without AUTH) |

Unauthenticated connections (when auth is required) may only send **AUTH**, **PING**, and **QUIT**. [`cli_client.cc`](client/cli_client.cc) sends **AUTH** when `--user` / `--password` are set, then **QUERY**.

### Responses

- Success: prefix **`OK|`**, then newline; first line is column names separated by **tab** `\t`; following lines are result rows, fields separated by `\t`. Implementation: [`Protocol::format_response`](src/network/protocol.cc). Auth success is `OK|authenticated\n`.
- Error: **`ERROR|<text>\n`** (e.g. `authentication required`, `permission denied`).

Limitation: a single read is capped at **4096** bytes on the server ([`Connection::read_message`](src/network/server.cc)) and **8192** bytes on the client when receiving—large queries or wide results need protocol changes (framing / length prefix).

### QUERY execution flow

```mermaid
sequenceDiagram
  participant Client
  participant Connection
  participant ThreadPool
  participant Database
  Client->>Connection: AUTH|user|password newline
  Connection-->>Client: OK|authenticated
  Client->>Connection: QUERY|sql newline
  Connection->>ThreadPool: submit handler
  ThreadPool->>Database: execute_query
  Database-->>ThreadPool: QueryResult
  ThreadPool->>Connection: format_query_result
  Connection->>Client: OK|... or ERROR|...
```

---

## Persistence

- Default directory: **`data/`** (CLI `--data-dir` / `Server` constructor).
- On startup: [`Database::load_from_disk`](src/core/database.cc) → [`PersistenceManager::load_database`](include/storage/persistence_manager.h).
- After successful mutations (**INSERT** / **UPDATE** / **DELETE** / **CREATE** / **ALTER**): only **dirty** tables are saved via atomic write (`.tmp` then rename) — [`persist_dirty_tables`](src/core/database.cc).
- **`Database::checkpoint()`** flushes all dirty tables through the WAL protocol, syncs the buffer pool, and truncates `wal.log` when safe.
- **DROP TABLE** deletes the corresponding `.db` file immediately.
- File format: magic **`0x44425442`** (`"DBTB"`), version **6** (schema + fixed-size heap pages); v1–v5 remain readable and upgrade on save ([`PersistenceManager`](include/storage/persistence_manager.h)).
- Buffer pool size: CLI **`--buffer-pool-pages <n>`** (default **64**).

---

## Backup and restore

Offline (cold) backup of the entire data directory. **Stop the server** before backup or restore. v1 does not support hot/online backup; if `.wal_stage_*.db` files are present (mid-commit), backup refuses.

### Operator steps

1. Stop `nobugdb` (Ctrl+C).
2. Backup:

```bash
make backup
# or:
./bin/db_backup backup --data-dir data --output-dir backup
```

3. Restore (replaces the data directory; `make restore` always passes `--force`):

```bash
make restore
# or without force (fails if data dir is non-empty):
./bin/db_backup restore --backup-dir backup --data-dir data
./bin/db_backup restore --backup-dir backup --data-dir data --force
```

4. Optional durability flush without a full copy:

```bash
./bin/db_backup checkpoint --data-dir data
```

### What is copied

- Everything under the data directory: `*.db`, `_views/`, `_routines/`, `_triggers/`, `_partitions/`, and related files.
- A `backup_manifest` listing relative paths, sizes, and **SHA-256** digests (`engine_version`, `created_at`).
- Auth file (`--auth-file`) is included **only** if it lives inside the data directory; otherwise copy it separately.

Implementation: [`BackupService`](include/storage/backup_service.h), CLI [`tools/db_backup.cc`](tools/db_backup.cc). Plan: [docs/plans/p2/03-backup-and-recovery.md](docs/plans/p2/03-backup-and-recovery.md).

---

## Supported SQL

### DDL

- **`CREATE TABLE`** — column definitions with types and modifiers; table-level and column-level `CHECK (expression)` (no subqueries/aggregates in v1; NULL makes the predicate UNKNOWN and the row is accepted).
- **`CREATE VIEW name AS <select>`** — stores the SELECT text; name must not collide with a table (and vice versa).
- **`DROP VIEW [IF EXISTS] name`**
- **`CREATE FUNCTION name(params) RETURNS type AS $$ [RETURN] expr; $$`** — or short `AS (expr)` / `AS expr`. Name must not collide with a builtin.
- **`DROP FUNCTION name`**
- **`CREATE PROCEDURE name(params) AS $$ stmt; ... $$`** — semicolon-separated statements; no nested TX-`BEGIN` in the body.
- **`DROP PROCEDURE name`**
- **`CREATE TRIGGER name BEFORE|AFTER INSERT|UPDATE|DELETE ON table FOR EACH ROW EXECUTE $$ ... $$`** — body statements; `SET NEW.col = expr` in BEFORE INSERT/UPDATE; `NEW`/`OLD` via correlation.
- **`DROP TRIGGER [IF EXISTS] name`**
- **`DROP TABLE`**
- **`ALTER TABLE`** — `ADD`/`DROP COLUMN`, `RENAME TO` / `RENAME COLUMN`, primary key / unique / not-null constraint changes, `ADD [CONSTRAINT name] CHECK (...)` / `DROP CHECK name`.

### DML

- **`INSERT INTO`** — `VALUES` for one or more rows (expressions allowed).
- **`CALL name(args)`** — invoke a stored procedure; IN params bind as locals for body statements.
- **`SELECT`** — see [Features](#features): single table or **JOIN** chains, **WHERE** (incl. **BETWEEN**), **DISTINCT**, **GROUP BY**, **HAVING**, **ORDER BY**, **LIMIT**, **OFFSET**, aggregates, scalar builtins / UDF, and window functions (`ROW_NUMBER` / `RANK` / `DENSE_RANK` / running `SUM` / `AVG` with `OVER`).
- **`UNION` / `UNION ALL` / `INTERSECT` / `EXCEPT`** — combine top-level SELECT (or parenthesized) operands; same width and exact types; trailing **ORDER BY** / **LIMIT** / **OFFSET** on the outermost expression only.
- **`UPDATE`** / **`DELETE`** — optional **WHERE**.
- **`EXPLAIN`** — run any statement and return its plan as a `QUERY PLAN` result set.

### Expressions in WHERE and SELECT

Comparisons, logical **AND** / **OR**, **NOT**, arithmetic `+ - * / %`, parentheses via the AST. Column identifiers and literals follow the parser rules. Scalar builtins and `CAST(expr AS type)` as above; untagged dollar-quoted strings `$$…$$` for routine bodies.

---

## Architecture

### Layers

1. **Client** — TCP connect, protocol lines, pretty-print (tabs shown as spaces).
2. **Network** — `accept` thread; per-client read thread [`Connection`](src/network/server.cc); work submitted to **`ThreadPool`**; implementation waits for each task before reading the next message on the same socket.
3. **Parser** — [`Lexer`](src/parser/lexer.cc) → [`Parser`](src/parser/parser.cc) → AST ([`ast.h`](include/parser/ast.h)).
4. **Routing** — [`Database::execute_query`](src/core/database.cc) dispatches by statement type to `execute_select_statement`, `execute_set_operation_statement`, `execute_insert_statement`, etc.
5. **Execution** — [`QueryExecutor`](include/executor/query_executor.h) classes: `SelectExecutor`, `JoinSelectExecutor`, `InsertExecutor`, `UpdateExecutor`, `DeleteExecutor`, `CreateTableExecutor`; [`ProcedureExecutor`](include/executor/procedure_executor.h) for `CALL`; [`TriggerExecutor`](include/executor/trigger_executor.h) around row mutations; [`SetOperationOperator`](include/executor/set_operation_operator.h) for UNION/INTERSECT/EXCEPT; shared [`SelectExpressionEvaluator`](include/executor/select_expression_evaluator.h) for row/column binding in SELECT and DML predicates (builtins via [`ScalarFunctionRegistry`](include/executor/scalar_function.h), UDF via [`RoutineCatalog`](include/core/routine_catalog.h)).
6. **SELECT post-processing** — [`SelectPipeline`](src/executor/select_pipeline.cc) chains [`IRelationalOperator`](include/executor/relational_operator.h) implementations after scan/join: group/aggregate → window → distinct → sort → limit/offset (see [`select_analysis.h`](include/executor/select_analysis.h) for grouping rules).
7. **Storage** — [`Table`](include/core/table.h) / [`Row`](include/core/row.h) / [`Column`](include/core/column.h); serialization via `PersistenceManager`.

```mermaid
flowchart LR
  Scan[SelectExecutor_or_JoinSelectExecutor]
  Group[GroupAggregateOperator]
  Win[WindowOperator]
  Distinct[DistinctOperator]
  Sort[SortOperator]
  Limit[LimitOffsetOperator]
  Scan --> Group
  Group --> Win
  Win --> Distinct
  Distinct --> Sort
  Sort --> Limit
```

### Patterns (conservative wording)

- **Strategy-like** split: separate executor classes sharing `QueryExecutor`; SELECT post-steps use **`IRelationalOperator`** (Open/Closed for new operators).
- **Singleton**: [`Logger`](include/utils/logger.h) only (`Logger::get_instance()`).
- **`Database`** is **not** a singleton: one instance per **`Server`**, passed into connections by pointer.
- Session teardown: `std::function` callback to unregister the connection ([`schedule_unregister_connection`](src/network/server.cc)).

---

## Concurrency and performance

- All **`Database::execute_query`** calls run under **`std::recursive_mutex`** ([`db_mutex_`](include/core/database.h)). Despite the thread pool, database work is **serialized**—no cross-client parallelism at the DB layer.
- **`ThreadPool`** is infrastructure for future work; the bottleneck today is one global lock for parse + execute.
- Typical cost: full table scan **O(n)** when no useful index; point/range lookups on PK/UNIQUE are **O(log n + k)** via B-tree; **INNER** equi **JOIN** may use in-memory hash join (**O(n + m)**) or index nested-loop; otherwise nested-loop (**O(n·m)**).

See [Limitations and future work](#limitations-and-future-work) for more.

---

## SQL examples

### Table and inserts

```sql
CREATE TABLE users (
  id INT PRIMARY KEY,
  name STRING NOT NULL,
  age INT,
  active BOOLEAN
);

INSERT INTO users (id, name, age, active)
VALUES (1, 'Alice', 30, true);

INSERT INTO users VALUES (2, 'Bob', 25, true);
```

### Views

```sql
CREATE VIEW adults AS SELECT id, name, age FROM users WHERE age >= 18;

SELECT * FROM adults;

DROP VIEW adults;
-- DROP VIEW IF EXISTS adults;
```

### Functions and procedures

```sql
CREATE FUNCTION double_it(x INT) RETURNS INT AS $$
  RETURN x * 2;
$$;

SELECT double_it(21);

CREATE PROCEDURE add_user(uid INT, uname STRING) AS $$
  INSERT INTO users (id, name) VALUES (uid, uname);
$$;

CALL add_user(3, 'Carl');
DROP FUNCTION double_it;
DROP PROCEDURE add_user;
```

### Triggers

```sql
CREATE TABLE audit (id INT);

CREATE TRIGGER audit_ins AFTER INSERT ON users FOR EACH ROW EXECUTE $$
  INSERT INTO audit VALUES (NEW.id);
$$;

CREATE TRIGGER bump BEFORE INSERT ON users FOR EACH ROW EXECUTE $$
  SET NEW.age = NEW.age + 1;
$$;

INSERT INTO users (id, name, age) VALUES (10, 'Dana', 20);
DROP TRIGGER audit_ins;
```

### Queries (reliable scenarios)

```sql
SELECT * FROM users;

SELECT name, age FROM users WHERE age > 25;

SELECT DISTINCT age FROM users;

SELECT name FROM users WHERE active = true AND age >= 30;

SELECT age FROM users ORDER BY age DESC LIMIT 5 OFFSET 0;
```

### Set operations

```sql
SELECT id FROM left_t UNION SELECT id FROM right_t;
SELECT id FROM left_t UNION ALL SELECT id FROM right_t;
SELECT id, name FROM left_t INTERSECT SELECT id, name FROM right_t;
SELECT id FROM left_t EXCEPT SELECT id FROM right_t ORDER BY id;
```

### Window functions

```sql
SELECT dept, amount,
       ROW_NUMBER() OVER (PARTITION BY dept ORDER BY amount) AS rn
FROM sales;

SELECT amount, RANK() OVER (ORDER BY amount) AS rnk FROM sales;
SELECT amount, SUM(amount) OVER (PARTITION BY dept ORDER BY id) AS running
FROM sales;
```

### Aggregation and grouping

```sql
CREATE TABLE sales (id INT PRIMARY KEY, dept STRING, amount INT);
INSERT INTO sales VALUES (1, 'A', 10), (2, 'A', 20), (3, 'B', 5);

SELECT dept, COUNT(*), SUM(amount), AVG(amount)
FROM sales
GROUP BY dept
HAVING COUNT(*) > 1
ORDER BY dept;

SELECT COUNT(*) FROM sales;
```

### Joins

```sql
CREATE TABLE orders (id INT PRIMARY KEY, user_id INT NOT NULL);
CREATE TABLE customers (id INT PRIMARY KEY, name STRING NOT NULL);
INSERT INTO orders VALUES (1, 10);
INSERT INTO customers VALUES (10, 'Carol');

SELECT orders.id, customers.name
FROM orders
INNER JOIN customers ON orders.user_id = customers.id;

-- Cartesian product (INNER without ON, or CROSS)
SELECT a.i, b.j FROM a INNER JOIN b;
SELECT a.i, b.j FROM a CROSS JOIN b;

-- FULL OUTER (unmatched rows from either side appear with NULLs)
SELECT fa.id, fb.id
FROM fa FULL OUTER JOIN fb ON fa.id = fb.id;
```

### Update and delete

```sql
UPDATE users SET age = 31 WHERE id = 1;

DELETE FROM users WHERE id = 2;
```

### Explain

```sql
EXPLAIN SELECT * FROM users WHERE id = 1;
EXPLAIN INSERT INTO users VALUES (3, 'Eve', 22, true);
```

There is still no **`table.\*`** qualifier (only `*` or qualified column names; multi-table `SELECT *` uses `alias.column` headers). See [SELECT limitations (v1)](#select-limitations-v1) for grouping and **ORDER BY** constraints.

---

## Logging and errors

Macros from [`logger.h`](include/utils/logger.h):

```cpp
DB_LOG_DEBUG("...");
DB_LOG_INFO("...");
DB_LOG_WARNING("...");
DB_LOG_ERROR("...");
```

Exception hierarchy in [`exceptions.h`](include/utils/exceptions.h):

| Type | Purpose |
|------|---------|
| `ParseException` | SQL parse errors |
| `ConstraintException` | Constraint violations |
| `NotFoundException` | Missing table or column |
| `TypeException` | Type errors |
| `InvalidOperationException` | Invalid operation |
| `StorageException` | Persistence I/O errors |
| `NetworkException` | Socket errors on the server |

At the network boundary many failures become **`ERROR|...`** text rather than C++ exceptions on the client.

---

## Limitations and future work

### Current limitations

- Page heap + buffer pool are present; no page locks / tablespaces / compression; a row must fit in one 8 KiB page.
- Auth is optional file-based TCP AUTH (SHA-256 of salt+password); no TLS, no per-table GRANT, not production-hardened.
- Cost-based planning uses row counts, per-column NDV, and equal-width histograms; hash join covers INNER equi only (no hash outer joins / disk hash).
- Request/response size bounded by fixed receive buffers.
- Scalar UDF / procedures: SQL-bodied only; untagged `$$` dollar-quotes; no OUT/INOUT or table-valued functions.
- Triggers: FOR EACH ROW only; no `WHEN` / `INSTEAD OF` / statement-level; recursion max 16.
- Partitioning: RANGE/HASH only; no SUBPARTITION / global indexes / FK on parents.
- Builtin sharding: static `shard_map.conf`; single-shard TX only; no cross-shard JOIN / 2PC / auto-rebalance.

### Possible enhancements

Step-by-step implementation plans (P0–P3): **[docs/plans/README.md](docs/plans/README.md)** (CHECK, views, auth, set ops, windows, hash join, functions/procedures, pages, partitions, backup, triggers, sharding).

1. Connection pooling and framed/streaming protocol for large payloads.
2. Window functions and richer **ORDER BY** (column ordinals).
3. Prepared-plan reuse beyond AST cache; richer statistics (multi-column hist).
4. Hash outer joins and fuller join reordering for outer joins.

---

## Extending the codebase

1. **New SQL statement** — extend [`Parser`](src/parser/parser.cc), add a `parse_statement` variant, handle it in `Database::execute_query`.
2. **New data type** — [`Value`](include/types/value.h), [`DataType`](include/types/data_type.h), [`TypeConverter`](include/types/type_converter.h), checks in [`Table`](src/core/table.cc).
3. **New constraint** — [`Column`](include/core/column.h) + INSERT/UPDATE validation.
4. **New relational operator** — implement [`IRelationalOperator`](include/executor/relational_operator.h) and register it in [`SelectPipeline`](src/executor/select_pipeline.cc) (existing examples: [`GroupAggregateOperator`](src/executor/group_aggregate_operator.cc), [`SortOperator`](src/executor/sort_operator.cc)).

For regression, run **`make test`** after pulling submodules; use the client and **`make demo`** for manual end-to-end checks.

---

## Statistics (approximate)

| Metric | Value |
|--------|-------|
| Headers + sources under `include/` + `src/` | **~50** files (`.h` / `.cc`) |
| Lines in `src/**/*.cc` + `main.cc` | **~5000** (version-dependent) |
| Test sources | **12** `tests/*.cc` + [`test_util.hh`](tests/test_util.hh); **~1100** lines in test `.cc` files (approx.) |
| Major subsystems | network, parser, executor, storage, types, threading |

---

## License and references

**NoBugDB** — standalone SQL engine focused on a clear client/server execution path and extensible internals. There are no bugs. Only undocumented features.

Useful references:

- SQL standard (ISO/IEC 9075)—conceptual background.
- C++17 and standard library documentation.
- Berkeley sockets / Winsock2 (abstracted in [`include/platform/`](include/platform/)).
- Database textbooks (e.g. Silberschatz, Korth, Sudarshan—*Database System Concepts*).

**Build:** `g++`, C++17, `-Iinclude`. **Targets:** Linux, macOS (POSIX backend), Windows MinGW-w64 (Winsock backend).
