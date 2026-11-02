# Module walkthrough

The implementation extends the original repository rather than importing the
larger sensor-monitoring-system project. Read this guide alongside the source.

## What changed

| Original code | Current behavior |
|---|---|
| One TCP client handled until disconnect | One `poll()` loop handles a dynamic client list |
| Blocking `accept`/`fgets` can delay shutdown | Nonblocking sockets and a 200 ms poll timeout |
| Consumer marks an item when copying it | Explicit `sbuffer_ack_for` after processing succeeds |
| Both readers wait at the same head node | Each role finds its first unacknowledged node |
| Data/storage workers only print consumed data | Analysis uses a room map; storage inserts into SQLite |
| All managers in `thread_workers.c` | Each manager has one source file; public thread entry names remain |
| Log write errors ignored | Partial/interrupted writes handled and failures reported |
| SIGINT directly changes the atomic run flag | Handler sets a `sig_atomic_t`; main coordinates worker shutdown |

## Suggested reading order

1. `inc/sbuffer.h`: the measurement struct and consumer roles.
2. `src/sbuffer.c`: insert, pop, acknowledgment, close.
3. `src/protocol.c` and `src/sensor_node.c`: text parsing and sending.
4. `src/connection_manager.c`: multiple clients and record assembly.
5. `src/data_manager.c`: per-sensor analysis.
6. `src/storage_manager.c`: prepared inserts and retries.
7. `src/logging.c` and `src/logger_proc.c`: process communication.
8. `src/main.c`: startup, thread lifetime, shutdown.

## 1. Data and shared buffer

`sensor_data_t` still has `sensor_id`, `value`, and `ts`. A node contains this
reading, two completion flags, and a next pointer. The original flag names
`read_by_data_mgr` and `read_by_storage_mgr` are retained, but now mean processing
has been acknowledged.

```text
push       -> DATA=0 STORAGE=0 -> retain
analyze    -> DATA=1 STORAGE=0 -> retain
SQL commit -> DATA=1 STORAGE=1 -> reclaim at the head
```

The existing function name `sbuffer_pop_for` is retained to reduce API changes;
it now only copies the first pending reading for a role. It does not remove or
acknowledge that reading. A consumer must call `sbuffer_ack_for` after success.
Repeating pop before ack returns the same reading.

There is exactly one worker per consumer role. The API is not a worker-pool API.
All list links, flags, and closed state are protected by the buffer mutex.
A condition variable puts empty readers to sleep. Always recheck the condition
in a loop because waking alone does not prove a suitable item exists.

SQL and logging happen outside the mutex. Closing wakes readers and permits a
drain; destruction happens only after every worker joins.

## 2. Text protocol and simulator

The original text format is preserved. `parse_sensor_line` validates a complete
line using `strtoul`, `strtod`, and `strtoll`, checking range errors and trailing
text. Parsing into temporary values avoids casting a timestamp pointer to a
different integer-pointer type.

The simulator uses socket/connect, formats a line, loops over partial sends, and
waits between readings. It sends a constant temperature so results are predictable.
Use `./sensor_node 127.0.0.1 6543 20 30 10 200` to send ten hot readings.

## 3. Connection manager

Each client node stores its descriptor, ID, line buffer, received length, last
activity time, and next pointer. This is a linked list like the shared buffer,
but only the connection worker accesses it, so it needs no separate mutex.

Each iteration builds a poll array for the listener and existing clients.
Readable clients receive a chunk of bytes. Bytes accumulate until newline; one
chunk may contain several records. Invalid or overlong records close that client
without stopping other clients. Once a connection identifies itself, its ID must
remain constant.

Sockets are nonblocking so a readiness race cannot trap the thread inside recv
or accept. The 200 ms poll timeout lets it observe the stop flag even when no
client sends anything. Idle clients are closed after ten seconds of no received
bytes, using `CLOCK_MONOTONIC`.

Existing nodes are processed before accepting a new one to preserve poll-array
index alignment. `previous` and `current` pointers unlink a closing client.
The number of clients is dynamic; OS memory and file descriptor limits still apply.

## 4. Data manager

The room mapping is loaded into a linked list at startup. Every room has an
independent temperature array, sum, count, and replacement index. For each reading:

1. Find the mapped sensor; log unknown IDs.
2. Subtract the value at the replacement index.
3. Replace it with the new value and add the new value to the sum.
4. Advance the index modulo the window size.
5. Once the window is full, compare its average with the thresholds.
6. Acknowledge analysis completion.

For a five-reading window, `10,20,30,20,20` averages to 20. Adding 30 removes the
old 10 and gives an average of 24. Averages stay in this module and are not added
to the shared buffer. Numerical sum overflow is treated as a fatal error.

## 5. Storage manager

SQLite is a library working on a file, not a separate server process. Initialization
opens the database, checks/creates the table, and prepares an INSERT with three
placeholders. Bind functions supply values safely.

```text
pop copy -> bind -> sqlite3_step -> ack -> reset statement
                         |
                       failure -> keep entry pending -> reconnect/retry
```

Successful autocommit INSERT is the storage acknowledgment boundary. Initialization
and per-reading writes have bounded attempt counts. A permanent failure requests
shutdown and leaves that reading unacknowledged until final memory cleanup.
See the protocol reference for exact retry behavior and storage schema.

## 6. Logging

`log_init` opens the FIFO writer with bounded startup retries so an early logger
failure does not hang startup forever. It then switches the descriptor to blocking
mode for ordinary writes. `log_event` formats one bounded line, removes embedded
newlines, and serializes writes under a mutex. It handles partial writes and EINTR.

The child opens the file and FIFO, reads lines, and adds a sequence and local-time
timestamp. Main sends `SHUTDOWN` after joining workers. It is a control line, not
a recorded event. The logger child ignores terminal SIGINT so main can finish the
normal shutdown sequence. SIGPIPE is ignored so a broken FIFO becomes an error.

## 7. Main and thread state

`gateway_ctx_t` contains the shared buffer pointer, port, and input/output paths.
It lives on main's stack until workers join. Buffer/client/room nodes live on the
heap. Each thread has its own stack but shares process memory; the logger process
has an independent address space and communicates through FIFO bytes.

`g_running` and `g_failed` retain the original global-flag approach and use atomics.
The signal handler only sets a `volatile sig_atomic_t`. Main checks state every
100 ms, monitors the logger child, and asks workers to stop. The connection worker
closes the buffer after it stops producing. Consumers drain, main joins them,
then it stops/reaps the logger and frees resources.

Exit status is zero for normal termination and nonzero for fatal failures.
SIGKILL bypasses the cleanup path. Use Ctrl+C or SIGTERM for normal stopping.
