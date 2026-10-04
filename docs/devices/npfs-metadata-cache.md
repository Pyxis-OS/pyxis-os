# npfs metadata read cache

The serial BSP filesystem worker maintains a clean physical-block cache for
directory pages, inode-file pages and indirect mapping pages in each retained
pool. These reads enter through `read_metadata_block` in
[the store](../../kernel/fs/npfs_store.c); regular file contents keep their
existing inode/logical-block cache. Headers, allocation bitmap, volume table,
journal payload and replay reads do not enter the metadata cache.

The cache retains at most 128 pages, or 512 KiB of payload per pool. Four 128 KiB
VM chunks are allocated lazily, with a separate physical-home array in pool
state. Insertion replaces pages in round-robin order. Allocation is best effort:
an unsuccessful cache allocation does not fail a successful backing read. The
worker owns the cache under the existing
[allocation and VM rules](../kernel/smp.md#memory-and-output-boundaries).

Journal images always overlay cached home pages. Reads of transaction images
never populate the cache, so a healthy abort only drops its journal overlay and
can immediately reuse the cached durable base. After a checkpoint reaches durable
EMPTY, it invalidates cached homes touched by the transaction before dropping the
images. A failed checkpoint retains those images and their read precedence.
Physical-block allocation invalidates any cached page at that home before the
block can become data or new metadata. This also covers metadata-to-data reuse.

Memory-pressure maintenance releases every metadata chunk, including when dirty
file writeback fails. It preserves transaction images and the retained allocation
bitmap. Later reads may allocate cache chunks again; this is reclamation by the
worker, not a new memory-admission or allocator-retry contract.

Directory lookup and cache-key lookup remain linear. A cache hit avoids backing
I/O but still traverses the BSP worker and copies a full 4 KiB page into store
scratch. The bounded cache does not guarantee that a metadata working set stays
resident. Mounted pools exclude raw writes, and concurrent external disk changes
remain unsupported under the
[kernel adapter contract](filesystem-native-adapter.md).
