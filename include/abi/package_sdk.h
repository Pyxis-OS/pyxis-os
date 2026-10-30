#ifndef ABI_PACKAGE_SDK_H
#define ABI_PACKAGE_SDK_H

#include <abi/package_commands.h>
#include <stdint.h>

#define DIRECTORY_PACKAGE_SDK UINT64_C(19)
/* The npfs volume name the sdk view requires, the packages volume. */
#define PACKAGE_SDK_VOLUME "packages"

/* A capture holds the whole index in kernel memory, bounded by the package-view
 * budget (package_command_info.budget), not by a constant. */
#define PACKAGE_SDK_PATH_MAX PACKAGE_COMMAND_PATH_MAX
/* Eight bytes: PXSDK and three zeros. */
#define PACKAGE_SDK_INDEX_MAGIC "PXSDK\0\0"

/* sets/N.sdk: little endian, this header, packages package records, files
 * file records and strings bytes of paths, without trailing bytes. The magic
 * is PXSDK padded with zeros and the set equals N. Any other header is a
 * damaged index. */
struct package_sdk_index_header {
  char magic[8];
  uint64_t set;
  uint32_t packages;
  uint32_t files;
  uint32_t strings;
  uint32_t reserved;
};

/* A dev package of the set. Records are sorted by unsigned id bytes with no
 * duplicate; strings terminate with NUL and all remaining bytes are zero. */
struct package_sdk_package {
  char id[65];
  char version[65];
  uint8_t reserved[6];
};

/* One file of the view, at the same path in its package's tree: a file under
 * include/, a file directly in lib/, or a file directly in lib/pkgconfig/.
 * path is a byte offset into the strings, which hold each record's path
 * followed by NUL, in record order. Records are sorted by unsigned path bytes
 * with no duplicate, and no path is also a directory of another. package
 * indexes the package records. Directories are implied by the paths. */
struct package_sdk_file {
  uint32_t path;
  uint32_t path_length;
  uint32_t package;
  uint32_t reserved;
};

/* PACKAGE_SDK uses a complete directory_message with a zero body. Called on a
 * held native root of a volume named PACKAGE_SDK_VOLUME with LOOKUP|READ_FILES
 * (any other volume is BAD_OPERATION), it returns
 * directory_child_reply: a live read-only view of the active set's sdk index,
 * with at most LOOKUP|ENUMERATE|READ_FILES. Each lookup or enumeration through
 * it reads current; the kernel keeps the index of the current set it last read,
 * one for the whole system, and reads an index again only when current names
 * another set. Directories it returns, and their files, belong to the index
 * read at that lookup and hold it until closed.
 *
 * COMMAND_CAPTURE on the live view returns a fixed view of one set's index:
 * with a zero previous, the current set; otherwise previous must be a fixed
 * command view of the same volume, and the view shows that capture's set. A set
 * without an index is damaged. No other package operation applies. */

_Static_assert(sizeof(struct package_sdk_index_header) == 32, "sdk index header layout");
_Static_assert(sizeof(PACKAGE_SDK_INDEX_MAGIC) == sizeof(((struct package_sdk_index_header *)0)->magic),
    "sdk index magic fills its field");
_Static_assert(sizeof(struct package_sdk_package) == 136, "sdk index package layout");
_Static_assert(sizeof(struct package_sdk_file) == 16, "sdk index file layout");

#endif
