# Linux Sensor Gateway

A small C application that receives temperature readings over TCP, calculates
per-sensor rolling averages, stores raw readings in SQLite, and writes events
through a separate logging process.

The project keeps the original `inc/` and `src/` layout and uses functions,
structs, linked lists, pthreads, and POSIX system calls. Sensors are simulated.

## Build

Requirements: a C11 compiler, Make, SQLite development headers, and Python 3.8+
for integration tests. The SQLite CLI is optional for inspecting the database.

On Ubuntu/Debian:

```sh
sudo apt install build-essential libsqlite3-dev sqlite3 python3
make
```

On macOS, install Command Line Tools and provide SQLite headers/library, then run
`make`. Run all commands below from the project root.

## Run

Terminal 1:

```sh
./gateway 6543
```

Terminal 2:

```sh
# IP, port, sensor ID, temperature, number of readings, interval in milliseconds
./sensor_node 127.0.0.1 6543 20 30 10 200
```

Terminal 3, optionally:

```sh
./sensor_node 127.0.0.1 6543 50 10 10 200
```

Sensor 20 maps to room 602 and sensor 50 maps to room 905. After five samples,
30 degrees Celsius produces a hot alert; 10 degrees produces a cold alert.
The simulator sends real TCP traffic; SQLite and FIFO logging are also real.

```sh
cat gateway.log
sqlite3 sensor.db 'SELECT sensor_id, temperature, timestamp FROM sensor_data ORDER BY id;'
```

Stop the gateway with Ctrl+C or SIGTERM. Accepted complete readings drain during
normal shutdown. A partial record not yet accepted can be discarded.

Custom paths are supplied together:

```sh
./gateway 6543 config/room_sensor.map demo.db
```

The gateway binds to `127.0.0.1`. Run one instance per working directory because
`logFifo` and `gateway.log` have fixed paths. Each run replaces the log; existing
database rows are preserved. `make clean` retains logs and databases.

## Data flow

```text
sensor_node -> TCP connection manager -> one shared buffer
                                         |           |
                                  data manager   storage manager
                                  averages/logs     SQLite

main and worker threads -> logFifo -> logger child -> gateway.log
```

The main process has main plus three worker threads. The logger is forked before
worker creation. Each buffered reading is acknowledged independently by analysis
and storage, and is freed only after both complete. Storage acknowledges after a
successful insert, not when it merely reads a copy.

## Text protocol

One newline-terminated reading per record:

```text
20 21.5 1700000000
```

Fields are sensor ID, temperature in degrees Celsius, and Unix timestamp in seconds.
This preserves the original repository's text interface. Multiple records can arrive
together or in fragments. See [protocol](docs/protocol.md) for validation rules.

## Configuration

`config/room_sensor.map` contains `ROOM_ID SENSOR_ID` pairs, one per line.
Each room and sensor must appear only once. No headers, comments, or blank lines.

Defaults in `inc/common.h`:

| Setting | Default |
|---|---:|
| Average window | 5 readings per sensor |
| Cold threshold | Below 18.0 degrees Celsius |
| Hot threshold | Above 26.0 degrees Celsius |
| SQLite attempts | 3 |
| Retry delay | 5 seconds |
| Client idle timeout | 10 seconds |

Alerts start after a full window. Every subsequent out-of-range average generates
an event; boundary values do not trigger an alert. Unknown positive sensor IDs
produce a diagnostic but remain stored as raw readings.

## Test and debug

```sh
make test
make debug
make sanitize
```

`make test` runs C unit tests and Python integration tests on localhost. Retry and
idle-timeout cases intentionally wait; the suite usually takes about one minute.
`make debug` adds per-reading logs. `make sanitize` rebuilds and tests with ASan/UBSan.
After changing build modes, use `make clean` then `make` to restore the default.
Do not run different build modes concurrently in the same checkout.

## Files

```text
inc/                     Public headers and settings
src/main.c               Startup, signals, thread joins, logger cleanup
src/thread_workers.c     Shared run/failure flags and fatal-error helper
src/connection_manager.c TCP clients and text framing
src/data_manager.c       Room map, rolling averages, alerts
src/storage_manager.c    SQLite inserts and retry policy
src/sbuffer.c            One buffer with two consumer acknowledgments
src/logging.c            Thread-safe FIFO writer
src/logger_proc.c        Child process writing numbered log entries
src/protocol.c           Text validation
src/sensor_node.c        Standalone sensor simulator
config/                  Sample room mapping
tests/                   Unit and integration tests
docs/                    Module walkthrough, protocol, and validation notes
```

## Documentation and scope

- [Module walkthrough and changes from the original code](docs/modules.md)
- [Protocol and storage behavior](docs/protocol.md)
- [Testing and limitations](docs/testing.md)

The software implements the gateway features described by the assignment. The
unprovided lab specifications cannot be used to verify exact wire/API compatibility;
this repository explicitly uses its original text protocol and the settings above.
No paper exercises are included.

Pending readings live only in an unbounded in-memory buffer. There is no crash
recovery, TLS, authentication, deduplication, or schema migration. This is a small
local project, not a production telemetry service.
