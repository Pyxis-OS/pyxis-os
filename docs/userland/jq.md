# jq

The normal image includes jq 1.7.1 at `bin://jq.pxe`; the shell resolves the bare
command. It is the upstream release built by the
[jq recipe](../../ports/jq/README.md), with two small patches, decNumber and no
Oniguruma. jq's MIT license and the dtoa and decNumber notices are installed under
`boot://share/licenses/jq`. Ported by owner request on 2026-10-13, as the one
planning-freeze exception for a port.

```text
jq . host://data.json
cat tmp://a.json | jq .name
jq -r .name home://package.json
jq -c . < tmp://a.json
jq --arg who world -n '"hello " + $who'
cat json+http://10.0.2.2:18080/jobs | jq '.jobs[].status'
```

jq reads files, scheme paths and stdin through libc, so it sees only what the
shell granted, and a path outside those grants fails like any other: an
unmounted `host://` or an unknown scheme reports `Could not open file ...: Not
found` with status 2. It writes only to stdout and stderr and needs no network,
launch or display authority. `$ENV` and `env` list the process environment, which
libc copies from the shell's.

## Behavior

Options, the language and exit statuses are upstream's: 0 on success, 1 under `-e`
when the last output is false or null, 2 for an unreadable file or bad usage, 3 for
a compile error and 5 for a runtime or input error. Number literals keep their
precision through decNumber (`123456789012345678901234567890` prints back
unchanged). Colors are on when stdout is a terminal, as on Unix; `-M` turns them
off.

Unavailable, by design or because libc lacks them:

- Regular-expression builtins (`test`, `match`, `capture`, `scan`, `sub` and the
  others) fail at run time with jq's message that it was compiled without
  Oniguruma.
- Builtins that need non-ISO math (`j0` to `yn`, `gamma`, `drem`, `significand`,
  `scalb`, `exp10`, `pow10`, `lgamma_r`) are undefined.
- `strptime` has no `%Z`; `now` has whole-second precision because libc has no
  `gettimeofday`; `mktime` follows `TZ`.

libc stdout starts unbuffered, so a patch makes jq's line buffered on a terminal
and fully buffered otherwise, as on Unix. A pipe or file then receives output when
the buffer fills or jq exits, unless `--unbuffered` is given. The other patch
builds `$ENV` from the environment snapshot, because libc has no `environ`.
jq also relies on libc returning a non-NULL pointer for `malloc(0)`, which it
does (see [libc portability](libc-portability.md#zero-size-allocation)).

## Qualification

Run on 2026-10-13 from the remote terminal with `scripts/run-qemu.sh` (QEMU/KVM,
4 CPUs, 2 GiB, VirtIO net, `ACCEL=kvm`) on the development host, not native
hardware, using an image built in the pinned builder image from the revisions this
page's pull request pins. The checks were interactive; nothing was automated or
added to the repository.

| Check | Result |
| --- | --- |
| `jq . tmp://a.json`, `cat tmp://a.json \| jq .name`, `-r`, `-c`, `--arg`, `-e` | Expected output; `-e` on `null` exits 1 |
| Filter error `error("boom")`; syntax error `jq 'if'` | Message, exits 5 and 3 |
| Truncated JSON; missing file; unknown scheme | `Unfinished JSON term at EOF`, exit 5; `Not found`, exit 2 |
| `jq -n '"abc" \| test("b")'` | The no-Oniguruma error, exit 5 |
| `fromdate`, `todate`, `strptime(...) \| mktime`, `tgamma`, `hypot`, `cbrt`, `fma`, `isnormal`, `$ENV` | Expected values |
| `cat json+http://10.0.2.2:18080/jobs \| jq '.jobs[].status'` against `tools/demo/ci-jobs-server.py` | Statuses `queued`, then `passed` and `failed` as the demo job progressed |

Launch cost: `jq -n 1` and `cat` of a small file each took about 0.04 s as seen
from the remote client, which includes the remote terminal's round trip, so jq's
own startup is not separable from that overhead. `jq -n '[range(1000000)]|length'`
took 0.34 s and parsing and summing a 100000-element array 0.26 s. Writing that
array with `jq -c > tmp://big.json` took 0.17 s; before the stdout patch it took
14.6 s, one native write per token.
The program image is 536 KiB (415 KiB code, 90 KiB read-only data, 27 KiB data and
zero-fill). Peak heap use was not measured.
