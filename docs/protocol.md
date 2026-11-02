# Protocol and storage reference

## TCP records

The gateway listens on localhost IPv4. One LF-terminated text record is:

```text
SENSOR_ID TEMPERATURE UNIX_TIMESTAMP
```

Example: `20 21.5 1700000000\n`. CRLF is accepted as trailing whitespace.
A record can span several recv calls; one recv can also contain several records.
Maximum line content is 255 bytes before newline. A final fragment without newline
is discarded. NUL bytes, oversized lines, malformed fields, non-finite values,
negative timestamps, ID zero, and IDs above 65535 are rejected by disconnecting
that client. Sensor timestamps must fit in `int64_t`; freshness is not checked.

The first complete record identifies the sensor for connection logs. Later ID
changes close the connection. Distinct connections can use the same ID and then
share analysis state; simulators should use unique IDs. Unknown positive IDs are
logged during analysis but still stored as raw readings.

A successful TCP send is not an application-level storage acknowledgment.
Transport is unencrypted and unauthenticated. This text format is the gateway's
application protocol; no alternate binary codec or protocol negotiation is provided.

## SQLite

```sql
CREATE TABLE IF NOT EXISTS sensor_data (
    id INTEGER PRIMARY KEY,
    sensor_id INTEGER NOT NULL,
    temperature REAL NOT NULL,
    timestamp INTEGER NOT NULL
);
```

The row ID differs from the sensor ID. Raw readings are retained across restarts.
There is no schema migration, duplicate suppression, or running-average column.
The database parent directory must already exist.

Initialization tries up to three times, waiting five seconds between failed
attempts. Each connection has a two-second SQLite busy timeout. A failed INSERT
is retried with a fresh connection after a delay. Each reading has at most three
write attempts, and each reconnect has its own initialization attempt limit.
Shutdown cancels retry sleeps but still permits pending work to be attempted.
Fatal storage failure exits nonzero. Pending uncommitted data exists only in RAM
and is lost when cleanup releases the buffer.

## Room mapping and analysis

Mapping lines contain `ROOM_ID SENSOR_ID`, both integers in 1..65535. Duplicate
rooms/sensors, empty maps, malformed lines, headers, comments, and blank lines
are rejected at startup. Edit the map and restart to apply changes.

The default averaging window is five samples per sensor, with strict comparisons
below 18.0 and above 26.0 degrees Celsius. No alert is issued before a full window.
Each later out-of-range average creates an event; there is no alert suppression.

## Logs

The logger writes:

```text
SEQUENCE YYYY-MM-DD HH:MM:SS MESSAGE
```

Time is local to the running process environment. Sequence numbers restart at one
and the log is replaced each run. Long messages are truncated to a bounded length.
Worker writes are serialized by a mutex. Logging is blocking; a hung logger or
full FIFO can stall workers. This implementation does not claim comprehensive
recovery from every logging or disk failure.
