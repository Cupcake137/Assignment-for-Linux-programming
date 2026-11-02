# Linux Sensor Gateway

[![Build and test](https://github.com/Cupcake137/Assignment-for-Linux-programming/actions/workflows/test.yml/badge.svg?branch=main)](https://github.com/Cupcake137/Assignment-for-Linux-programming/actions/workflows/test.yml)

A multithreaded temperature monitoring service built with C11 and POSIX APIs.
The gateway ingests concurrent TCP sensor streams, evaluates rolling temperature
averages, persists raw measurements to SQLite, and records events through a
separate logging process. A sensor simulator supports repeatable local workloads.

**Stack:** C11, POSIX threads, TCP sockets, `poll()`, `fork()`, named FIFO, SQLite, Make.

## Capabilities

- Concurrent sensor connections with per-client record assembly and idle timeouts.
- One shared buffer with independent analysis and storage acknowledgments.
- Per-sensor rolling averages, room mapping, and hot/cold events.
- SQLite prepared inserts, persistent records, and bounded retry attempts.
- Serialized FIFO events and a dedicated logger process.
- Coordinated SIGINT/SIGTERM shutdown and resource cleanup.
- Unit tests, failure-injection integration tests, and Ubuntu sanitizer CI.

## Architecture

```text
Sensor simulators --TCP--> Connection manager --> Shared buffer
                                                   |       |
                                            Data manager  Storage manager
                                            averages/alerts   SQLite

Main and worker threads --FIFO--> Logger process --> gateway.log
```

The gateway process contains main plus three worker threads. Buffer entries remain
allocated until both consumers acknowledge completion. Storage acknowledges only
after the database insert succeeds. The logger is forked before workers start.

[Architecture reference](docs/architecture.md)

## Build

Dependencies: C11 compiler, Make, SQLite development files. Python 3.8+ is required
for integration tests; the SQLite CLI is optional for database inspection.

Ubuntu/Debian:

```sh
sudo apt install build-essential libsqlite3-dev sqlite3 python3
make
```

The project is also validated on macOS with Apple Clang and SQLite. Commands below
use the project root as the working directory.

## Usage

```text
gateway PORT [MAP_PATH DB_PATH]
sensor_node IP PORT SENSOR_ID TEMPERATURE COUNT INTERVAL_MS
```

Start the gateway:

```sh
./gateway 6543
```

Send ten readings of 30 degrees Celsius, 200 ms apart, from another terminal:

```sh
./sensor_node 127.0.0.1 6543 20 30 10 200
```

The sample mapping associates sensor 20 with room 602. After five readings, its
average exceeds the hot threshold and generates an event.

```sh
cat gateway.log
sqlite3 sensor.db 'SELECT sensor_id, temperature, timestamp FROM sensor_data ORDER BY id;'
```

Custom map and database paths must be supplied together:

```sh
./gateway 6543 config/room_sensor.map demo.db
```

The gateway binds to `127.0.0.1`. Run one instance per working directory because
FIFO and log paths are fixed. Startup replaces the log and preserves existing
database rows. SIGINT or SIGTERM stops ingestion and drains accepted readings
during normal shutdown.

## Configuration

`config/room_sensor.map` contains unique `ROOM_ID SENSOR_ID` pairs, one per line.
Settings are defined in `inc/common.h` and take effect after rebuilding.

| Setting | Default |
|---|---:|
| Rolling window | 5 readings per sensor |
| Cold threshold | Below 18.0 degrees Celsius |
| Hot threshold | Above 26.0 degrees Celsius |
| SQLite attempt limit | 3 |
| Retry delay | 5 seconds |
| Client idle timeout | 10 seconds |

Alerts require a full window and use strict threshold comparisons. Every later
out-of-range average emits an event. Unknown positive sensor IDs are logged while
their valid raw readings remain eligible for storage.

[Protocol, configuration, and persistence reference](docs/protocol.md)

## Validation

```sh
make test       # Unit and integration tests
make sanitize   # Rebuild and test with AddressSanitizer/UndefinedBehaviorSanitizer
make debug      # Rebuild with per-measurement logging
make clean      # Remove build output; retain database and logs
```

Regular and sanitizer suites have passed on Ubuntu CI and macOS. Coverage includes
concurrent clients, fragmented records, rolling-average boundaries, restart
persistence, transient/permanent database failures, and idle-client shutdown.

[Coverage and validation details](docs/testing.md)

## Repository layout

```text
inc/        Public interfaces and compile-time settings
src/        Gateway modules and sensor simulator
config/     Sample room-to-sensor mapping
tests/      C unit tests and Python integration tests
docs/       Architecture, protocol, and validation references
.github/    Ubuntu CI workflow
Makefile    Build, debug, test, and sanitizer targets
```

Generated executables, object files, logs, FIFO, and database files are excluded
from version control.

## Operational limits

The buffer is unbounded and memory-only: pending readings are not recovered after
a crash. The protocol has no authentication, encryption, or storage acknowledgment.
There is no deduplication, schema migration, alert suppression, or configuration
reload. Blocking logger I/O can stall workers. These constraints define the
current scope of the service.
