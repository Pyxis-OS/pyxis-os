# Packaged timezone data

The `tzdata` port pins IANA **2026d** data and code to commit
`d633fe7ed3de8e00ce7cac991376a064a1373bb1`. Its host-built `zic` comes from that
same source. Builds do not consult the host's installed zic or timezone files,
and do not install/change the host's local timezone. The selected upstream
code/data are public domain; the complete license notice is retained.

The ports manifest installs the full standard zone database and aliases at
`app://share/zoneinfo/`. This is upstream's `main` data with `backward`, without
the optional alternative `backzone` history. There is no regional subset.
Aliases such as `UTC` are independent regular files, including their payloads;
the current initrd reader does not need hard-link or symbolic-link support.

Compilation uses `zic -b slim`, with no date-range truncation and no leap-second
table. These are standard TZif files with historical transitions and future-rule
footers. Leap-counting `right` data is deliberately absent because Pyxis uses
Unix seconds. No default localtime link is installed.

The directory also contains `tzdata.zi`, `version`, `iso3166.tab`, `zone.tab`,
`zone1970.tab` and `zonenow.tab`. Build provenance is at
`app://share/tzdata/source.txt`; licensing is at
`app://share/licenses/tzdata/LICENSE`. Host zic and intermediate outputs remain
in the disposable port work directory, outside the guest payload.

`make ports` builds the data recipe alongside the executable ports. Changes to
target SDK files do not rebuild the TZif data. The data travels in the existing
ports bundle and boot archive; no SDK, syscall or kernel-reader change is needed.
Update the version and exact source pin together in the recipe when updating the
database. The source/compiler and selection policy are described in
[the port notes](../ports/tzdata/README.md).

Libc's [local-time conversion](timezones.md) reads this data and its future-rule
footers. Absent or empty `TZ` means UTC; named zones are selected explicitly.
The default [session configuration](session-configuration.md) supplies the
explicit Bucharest selection through the environment.
