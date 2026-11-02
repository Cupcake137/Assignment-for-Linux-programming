# Testing

Run from the repository root:

```sh
make test
make sanitize
```

Tests require localhost TCP access. The Python suite creates isolated temporary
working directories and does not change your root log or database. Retry and idle
cases take about one minute in total. The test runner applies timeouts and kills
its process group if a test fails to avoid leaving logger children running.

## Automated coverage

| Area | Check |
|---|---|
| Buffer | 2,000 readings, independent readers, delayed storage, read-before-ack, ordered drain, invalid role, insert after close |
| Parser | Valid text/CRLF, bounds, overflow, trailing garbage, NaN/infinity, map validation |
| TCP | Five simultaneous sensors, fragmented/coalesced lines, oversized/partial input, ID switch |
| Simulator | Compiled C sender interoperates with gateway |
| Analysis | Full-window warm-up, rolling replacement, exact threshold boundaries, hot/cold alerts, room IDs |
| Logging | Sequence numbering and shutdown event |
| Persistence | 509 rows before restart; 510 after one additional reading |
| Retry | Database unavailable at startup then recovers without losing pending data |
| Failure | Three initialization failures stop the gateway |
| Insert | SQLite triggers inject transient and persistent write failures |
| Configuration | Duplicate map entries stop startup |
| Lifecycle | SIGTERM with an idle partial-line connection; receive idle timeout |

These are functional checks, not throughput benchmarks. ASan/UBSan can detect
many memory and undefined-behavior defects but do not prove race freedom.
Valgrind/ThreadSanitizer and exhaustive resource-failure tests are not included.

## Known limitations

The shared buffer has no capacity limit or disk-backed recovery. Reads scan from
the head, so a lagging consumer can increase memory use and search cost. The poll
array is rebuilt each iteration. No strict mutex fairness guarantee is claimed.
The service is intended for local simulated sensors and has no authentication,
TLS, deduplication, configuration reload, or database schema migration.

Linux CI is defined in `.github/workflows/test.yml`. Validation results should be
checked on the actual workflow run; a workflow definition alone is not evidence
of a successful Linux run. Local test results are recorded in the pull request.

## Local validation

On 2026-11-02, the regular `make test` and `make sanitize` suites passed on
macOS arm64 with Apple Clang 21. Both C unit tests and all eight integration
scenarios completed successfully. No ASan/UBSan diagnostics were reported.
`git diff --check`, documentation link checks, and English text checks passed.
Linux results are tracked separately in the pull request's workflow runs.
