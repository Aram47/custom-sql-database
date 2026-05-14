# SQL Database Engine with CRUD Operations

A high-performance, modular C++ SQL database engine featuring full CRUD operations, query parsing, persistence, multi-threaded execution, and network support via sockets.

## Features

### 1. **SQL Query Parser**
- Lexical analysis and tokenization
- Recursive descent parser with operator precedence
- Support for SELECT, INSERT, UPDATE, DELETE, CREATE TABLE statements
- WHERE clause filtering
- JOINs (INNER, LEFT, RIGHT)
- Aggregate functions (COUNT, SUM, AVG, MIN, MAX)
- GROUP BY and HAVING clauses
- ORDER BY with ASC/DESC
- DISTINCT keyword

### 2. **CRUD Operations**
- **CREATE TABLE**: Dynamic table schema creation with type support
- **SELECT**: Complex queries with WHERE, JOINs, GROUP BY, ORDER BY
- **INSERT**: Single and multi-row inserts with constraint validation
- **UPDATE**: Selective updates with WHERE conditions
- **DELETE**: Row deletion with WHERE filtering

### 3. **Data Type System**
- INT (64-bit signed integers)
- FLOAT (double precision floating point)
- STRING (variable-length text)
- BOOLEAN (true/false)
- DATE (YYYY-MM-DD format)
- UUID (RFC 4122 format)

### 4. **Advanced Constraints**
- Primary Key constraints
- Unique constraints
- NOT NULL constraints
- Automatic validation on INSERT/UPDATE/DELETE

### 5. **Persistence Layer**
- Binary file format for efficient storage
- Automatic table serialization/deserialization
- Data durability across server restarts
- Directory-based database storage

### 6. **Multi-threaded Architecture**
- Thread pool with configurable worker count
- Thread-safe task queue
- Asynchronous query execution
- Graceful shutdown with worker synchronization

### 7. **Network Support**
- TCP socket server (default port 9000)
- Custom protocol for request/response
- Per-client connection handlers
- Signal handling for clean shutdown

### 8. **CLI Client**
- Interactive query mode
- Batch file execution
- Tab-separated result formatting
- Connection management

## Project Structure

```
lesson_47/
├── include/
│   ├── core/
│   │   ├── Column.h
│   │   ├── Row.h
│   │   ├── Table.h
│   │   └── Database.h
│   ├── parser/
│   │   ├── Token.h
│   │   ├── Lexer.h
│   │   ├── AST.h
│   │   └── Parser.h
│   ├── executor/
│   │   └── QueryExecutor.h
│   ├── storage/
│   │   └── PersistenceManager.h
│   ├── threading/
│   │   ├── WorkQueue.h
│   │   └── ThreadPool.h
│   ├── network/
│   │   ├── Protocol.h
│   │   └── Server.h
│   ├── types/
│   │   ├── DataType.h
│   │   ├── Value.h
│   │   └── TypeConverter.h
│   └── utils/
│       ├── Logger.h
│       └── Exceptions.h
├── src/
│   ├── core/
│   ├── parser/
│   ├── executor/
│   ├── storage/
│   ├── threading/
│   ├── network/
│   ├── types/
│   └── utils/
├── client/
│   └── cli_client.cpp
├── data/
│   └── (persisted .db files)
├── main.cpp
├── Makefile
└── README.md
```

## Build Instructions

### Prerequisites
- C++17 compatible compiler (g++ 7.0+, clang++ 5.0+)
- Linux/POSIX system
- pthread library (standard on Linux)

### Build the Server
```bash
make build
# Output: bin/db_server
```

### Build the Client
```bash
make client
# Output: bin/db_client
```

### Build Everything
```bash
make all
```

## Usage

### Starting the Server

```bash
make run
# Server listens on localhost:9000
# Press Ctrl+C to stop
```

### Running the Interactive Client

In another terminal:

```bash
make client-run
# Connects to server at localhost:9000
# Type SQL queries or 'help' for assistance
```

### Batch Mode

```bash
./bin/db_client query_file.sql
# Executes queries from file
```

### Demo

```bash
make demo
# Automatically starts server, runs sample queries, and exits
```

## SQL Examples

### Create Table

```sql
CREATE TABLE users (
  id INT PRIMARY KEY,
  name STRING NOT NULL,
  age INT,
  active BOOLEAN
);
```

### Insert Data

```sql
INSERT INTO users (id, name, age, active)
VALUES (1, 'Alice', 30, true);

INSERT INTO users VALUES (2, 'Bob', 25, true);
INSERT INTO users VALUES (3, 'Charlie', 35, false);
```

### Select Queries

```sql
-- Simple select
SELECT * FROM users;

-- Select specific columns
SELECT name, age FROM users;

-- With WHERE clause
SELECT * FROM users WHERE age > 25;

-- With comparison operators
SELECT name FROM users WHERE age >= 30 AND active = true;

-- With ORDER BY
SELECT * FROM users ORDER BY age DESC;

-- With DISTINCT
SELECT DISTINCT age FROM users;
```

### Update Data

```sql
UPDATE users SET age = 31 WHERE id = 1;
UPDATE users SET active = false WHERE age < 30;
```

### Delete Data

```sql
DELETE FROM users WHERE id = 2;
DELETE FROM users WHERE active = false;
```

## Architecture & Design Patterns

### Layered Architecture

1. **Network Layer**: TCP socket server with per-connection handlers
2. **Query Parser Layer**: SQL tokenization and AST construction
3. **Query Executor Layer**: Strategy pattern for different SQL operations
4. **Storage Layer**: In-memory tables with binary persistence
5. **Threading Layer**: Thread pool for async query execution
6. **Type System**: Variant-based polymorphic values

### Design Patterns Used

- **Strategy Pattern**: Different executors for SELECT, INSERT, UPDATE, DELETE
- **Factory Pattern**: QueryDispatcher creates appropriate executors
- **Thread Pool Pattern**: Fixed workers processing tasks from queue
- **Singleton Pattern**: Logger and Database instances
- **Observer Pattern**: Connection callbacks for async results

### SOLID Principles

- **Single Responsibility**: Each class has one reason to change
- **Open/Closed**: QueryExecutor interface is extensible
- **Liskov Substitution**: All executors implement QueryExecutor interface
- **Interface Segregation**: Minimal, focused interfaces
- **Dependency Inversion**: Depends on abstractions, not concretions

## Performance Characteristics

- **Query Parsing**: O(n) lexical pass + O(n) recursive descent
- **Row Lookup**: O(n) linear scan (can be optimized with B-tree indexing)
- **Insert/Update/Delete**: O(n) worst case with constraint checking
- **Select with WHERE**: O(n) row scan with expression evaluation
- **Multi-threading**: Configurable thread pool (default 4 workers)

## Limitations & Future Improvements

### Current Limitations
- No transaction support (ACID compliance)
- Linear scan for queries (no advanced indexing)
- No query optimization or cost-based planner
- No support for OUTER/FULL JOINs
- No subquery support
- No authentication/authorization

### Potential Enhancements
1. B-tree indexing for faster lookups
2. Query optimizer with cost estimation
3. Transaction support with ACID guarantees
4. Connection pooling and multiplexing
5. Prepared statement caching
6. Query result caching
7. Partitioning and sharding
8. Backup and recovery mechanisms
9. Replication support
10. Web-based query interface

## Code Statistics

- **Total Files**: 30+
- **Total Lines**: ~4,000+
- **Core Layers**: 6 (Network, Parser, Executor, Storage, Threading, Types)
- **Classes**: 40+
- **Test Coverage**: Foundation and basic CRUD operations

## Logging

The engine uses a built-in logger with configurable log levels:

```cpp
DB_LOG_DEBUG("Debug message");
DB_LOG_INFO("Info message");
DB_LOG_WARNING("Warning message");
DB_LOG_ERROR("Error message");
```

## Error Handling

Comprehensive exception hierarchy for different error scenarios:

- `ParseException`: SQL parsing errors
- `ConstraintException`: Constraint violations
- `NotFoundException`: Table/column not found
- `TypeException`: Type conversion errors
- `StorageException`: File I/O errors
- `NetworkException`: Socket errors
- `InvalidOperationException`: Invalid operations

## Contributing

To extend the database:

1. **Add new SQL statement type**: Extend Parser class
2. **Add new executor**: Implement QueryExecutor interface
3. **Add new data type**: Extend Value and TypeConverter
4. **Add new constraint**: Extend Table validation logic

## License

Educational project for learning SQL database internals.

## References

- SQL Specification (ISO/IEC 9075)
- C++ Standard Library Documentation
- POSIX Socket Programming
- Database System Concepts (Silberschatz, Korth, Sudarshan)

---

**Built with**: C++17, Standard Library, POSIX Sockets
**Compiler**: g++ 7.0+ or clang++ 5.0+
**Target Platform**: Linux/POSIX
# custom-sql-database
