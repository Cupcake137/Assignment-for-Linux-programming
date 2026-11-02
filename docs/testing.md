# Validation reference

## Test execution

| Command | Coverage |
|---|---|
| `make test` | C unit tests and Python integration suite |
| `make sanitize` | Clean rebuild and the same suite under ASan/UBSan |

The suite requires Python 3.8+, SQLite, and localhost TCP access. Integration cases
use temporary working directories and isolated process groups. Database retries
and the idle-timeout case intentionally wait; a full run takes about one minute.
Timeout cleanup prevents failed cases from leaving logger processes behind.

Sanitizer and debug targets clean before rebuilding. Make does not track arbitrary
compiler-flag changes; restore the default mode with `make clean` and `make`.
Build modes must not run concurrently in one checkout.

## Coverage

| Area | Verification |
|---|---|
| Buffer | 2,000 ordered readings, independent consumers, delayed storage, read-before-ack, close/drain, invalid roles |
| Parser | Valid LF/CRLF records, numeric ranges, overflow, extra fields, non-finite input, mapping syntax |
| Connections | Five simultaneous senders, fragmented/coalesced lines, oversized/partial records, ID changes |
| Simulator | C sender interoperability with the gateway |
| Analysis | Window warm-up, rolling replacement, strict boundaries, room IDs, hot/cold events |
| Logging | Sequential numbering and shutdown events |
| Persistence | 509 rows before restart and 510 after one additional reading |
| Startup recovery | Database becomes available while readings remain pending |
| Failure limits | Three unsuccessful database initialization attempts terminate the service |
| Insert recovery | SQLite triggers inject transient and permanent write failures |
| Configuration | Duplicate mapping entries reject startup |
| Lifecycle | SIGTERM with a connected idle client and incomplete input; receive idle timeout |

Counts are functional expectations, not throughput measurements. The suite does
not benchmark sustained load or demonstrate race freedom under every schedule.

## Verified environments

On 2026-11-02, regular and sanitizer suites passed in both environments:

- macOS arm64 with Apple Clang 21 and system SQLite.
- Ubuntu through GitHub Actions, using the system C compiler and SQLite package.

[Verified Ubuntu run](https://github.com/Cupcake137/Assignment-for-Linux-programming/actions/runs/41458585103)

No ASan/UBSan findings were reported. Documentation links, English text, and
`git diff --check` were also validated. Current branch checks are published by
[the CI workflow](https://github.com/Cupcake137/Assignment-for-Linux-programming/actions/workflows/test.yml).

## Validation boundaries

Valgrind and ThreadSanitizer have not been run. Full disks, memory exhaustion,
logger hangs, every SQLite error code, and sustained saturation are not exhaustively
tested. ASan/UBSan success is not a proof of race freedom or complete correctness.
