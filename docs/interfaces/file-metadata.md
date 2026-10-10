# File and directory metadata

`FILE_INFO` takes an empty FILE payload and requires READ or WRITE.
`DIRECTORY_INFO` takes a complete zeroed directory message and requires no
additional content rights. Both return the 48-byte
[file_info_reply](../../include/abi/file_info.h). Neither traverses names,
delegates authority nor freezes contents. Operational failures publish no reply.

Each field has independent validity: SIZE, DOMAIN, OBJECT and MTIME. OBJECT
requires DOMAIN. Unknown fields have no usable value; zero is not an absence
marker. Different known domains prove distinctness without object IDs. Within
one domain, both object IDs are needed to compare identity.

## Identity and lifetime

The 64-bit domain/object pair describes a live backend object within one boot.
Keep a reference to the original object throughout comparison. Rename preserves
identity; replacement lookups identify the new object while retained victim
handles still identify the old one. There is no cross-boot or after-final-close
promise. Paths, capability slots and directory generations are not identities.

| Backing | Domain and object |
| --- | --- |
| RAM | Common RAM domain; ID allocated before publishing each object, shared by delegated paths |
| Archive | Separate archive domain; ID of the retained imported object |
| npfs | Mounted volume domain; retained inode ID, renewed when a free inode slot is reused |
| HOST | Session domain; live mapping of the complete FUSE node incarnation, shared by same-mount hardlinks and repeated roots |
| HTTP/text private snapshots | Reserved private-byte domain, disjoint from native backing; object ID unavailable |

Native IDs never wrap or collide. HOST mapping entries retire with their last
wrapper; transport lookup/open ownership remains separate. HOST bind aliases,
cross-session comparisons and continuity after final FORGET are excluded.
Generic providers/proxies must report their actual domain or leave it unknown;
being exported alone does not establish distinctness.

## Modification time and libc

MTIME is sampled signed Unix seconds with a normalized nanosecond fraction.
HOST uses fresh GETATTR; npfs uses its existing modified_ns validity, including
accepted cached writes. RAM records real wall time at creation and successful
content/namespace changes, clearing validity if wall time is unavailable.
Authored archive time is unknown. No filesystem format change is needed.
Time may repeat or move backwards; equality never proves unchanged contents.

`file_info()` and `directory_info()` validate replies and clear outputs on
failure. Only unsupported INFO (`BAD_OPERATION`) permits fallback: FILE reports
SIZE alone, DIRECTORY reports no optional metadata. Other errors stay errors.

Libc `stat`, `lstat` and `fstat` preserve type/size behavior and expose `st_dev`,
`st_ino` and `st_mtim` only under `STAT_DEV_VALID`, `STAT_INO_VALID` and
`STAT_MTIME_VALID` in `st_valid`. Device/inode types are lossless 64-bit native
IDs, not host device encoding. Directories keep size zero; special streams
provide type without invented identity/time. Callers must inspect validity.

The [shell](../userland/shell.md#file-redirection-and-stdin) checks domains and
identities before truncating explicit output redirects. [TCC](../userland/tcc.md)
retains once-header streams until translation-unit cleanup. Metadata supplies
neither a mutation lease nor a name-bound rename/remove precondition.
