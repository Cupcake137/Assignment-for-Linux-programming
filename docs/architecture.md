# Architecture reference

## Process and thread model

The application provides two executables: `gateway` and `sensor_node`. Each
simulator is an independent TCP client. The gateway starts a logger child with
`fork()` before creating any worker threads.

| Component | Responsibility | Source |
|---|---|---|
| Main thread | Arguments, resources, signals, joins, child monitoring | `src/main.c` |
| Connection manager | Client lifecycle, framing, validation, ingestion | `src/connection_manager.c` |
| Data manager | Room mapping, rolling windows, temperature events | `src/data_manager.c` |
| Storage manager | Database initialization, inserts, retries | `src/storage_manager.c` |
| Shared buffer | Two-consumer retention and synchronization | `src/sbuffer.c` |
| FIFO writer | Serialized event delivery from parent threads | `src/logging.c` |
| Logger process | Sequence/timestamp formatting and file output | `src/logger_proc.c` |
| Protocol parser | Numeric bounds and record validation | `src/protocol.c` |
| Sensor simulator | Configurable constant-temperature TCP workload | `src/sensor_node.c` |
| Coordination state | Atomic run/failure flags and fatal-error handling | `src/thread_workers.c` |

`gateway_ctx_t` contains the buffer, port, and map/database paths. Main owns the
context until all workers join. Worker threads share the parent address space and
have separate stacks. Client, room, and buffer nodes are heap allocations. The
logger has an independent address space and receives bytes through the FIFO.

## Ingestion

The connection manager maintains a dynamic linked list of clients and rebuilds a
poll array each iteration. Nonblocking sockets prevent readiness races from
blocking accept or receive operations. A 200 ms poll timeout bounds idle checks
and observation of the stop flag.

Each client stores its ID, partial line, byte count, and monotonic activity time.
Received chunks may contain partial or multiple records. Complete valid records
are appended to the shared buffer. The first record establishes a connection's
sensor ID; a subsequent ID change disconnects that client.

Existing client nodes are processed before newly accepted nodes are inserted,
preserving the array/list index mapping. Idle and invalid clients are released
without terminating other connections. OS descriptor and memory limits bound the
number of connections; there is no configured startup maximum.

## Shared buffer contract

`sensor_data_t` contains `sensor_id`, temperature `value`, and timestamp `ts`.
One linked list stores readings for both consumer roles. Node flags
`read_by_data_mgr` and `read_by_storage_mgr` represent acknowledged completion.

| Operation | Contract |
|---|---|
| `sbuffer_push` | Append a new node and wake waiting consumers |
| `sbuffer_pop_for` | Copy the first unacknowledged reading for one role |
| `sbuffer_ack_for` | Complete that role's current reading and reclaim eligible head nodes |
| `sbuffer_close` | Reject new writes; wake readers to drain and exit |
| `sbuffer_destroy` | Release nodes and synchronization objects after all workers join |

Pop does not acknowledge a reading. Repeated pops before acknowledgment return
the same item. Exactly one worker uses each role and performs pop/process/ack in
order. This API does not support several workers sharing a consumer role.

```text
Enqueued           analysis=0 storage=0   retained
Analysis completed analysis=1 storage=0   retained
Insert committed   analysis=1 storage=1   reclaimable at the list head
```

A mutex protects links, flags, and closed state. Readers wait on a condition
variable while no pending item exists; the condition is rechecked after every
wake-up. Insertion broadcasts to both roles. SQL and logging execute outside the
buffer critical section.

Reads scan from the list head. A lagging consumer increases both retention and
search costs. There is no capacity bound or backpressure policy.

## Temperature analysis

The data manager loads unique room/sensor pairs at startup. Each sensor has an
independent circular array, running sum, count, and replacement index. Updating
a reading subtracts the replaced slot, adds the new value, and advances the index
modulo the configured window length.

Comparisons start only after a full window. Running averages remain local to the
data manager and do not modify raw buffered measurements. Unknown IDs generate
diagnostics and still receive an analysis acknowledgment. A non-finite running
sum requests fatal shutdown.

## Persistence

The storage manager owns its SQLite connection and prepared insert statement.
Each insert runs in autocommit mode. The completion boundary is a successful
`sqlite3_step`, followed by a storage acknowledgment and statement reset.

Failed writes remain unacknowledged during reconnect/retry. Attempt limits apply
to database initialization and individual writes. A terminal failure requests
shutdown and leaves pending readings for final memory cleanup.

[Schema and retry policy](protocol.md)

## Logging

Parent threads format bounded events and serialize FIFO writes with a mutex.
Partial writes and interrupted calls are handled. Embedded newlines are replaced
so one event remains one record. Startup has a bounded FIFO-open retry window.

The logger opens the output file and FIFO, formats local timestamps and sequence
numbers, and flushes each event. The `SHUTDOWN` control record is not written as an
event. Main sends it after workers join and then waits for the child.

Logging is blocking. Logger stalls or full FIFOs can delay workers; comprehensive
I/O hang recovery is outside the current implementation.

## Lifecycle and error handling

SIGINT/SIGTERM handlers set a `volatile sig_atomic_t` flag. Main observes it every
100 ms and updates atomic worker state. Main also monitors logger exit/failure.

On normal termination:

1. The connection manager stops ingestion and closes client sockets.
2. The producer closes the shared buffer.
3. Consumers drain pending work and return.
4. Main joins workers and releases the buffer.
5. Main stops/reaps the logger and removes the FIFO.

A fatal worker error sets failure/stop state and closes the buffer to wake readers.
Partial thread-start failures join only workers that successfully started.
Exit status zero indicates normal termination; a nonzero status indicates failure.
SIGKILL or a crash bypasses cleanup and may lose pending data.

## Concurrency boundaries

The current lock ordering keeps database operations and FIFO writes outside the
buffer mutex. No path acquires buffer and logging mutexes in opposite orders.
POSIX mutexes do not guarantee FIFO fairness or a bounded wait time. Short critical
sections reduce contention but do not establish a starvation guarantee.
