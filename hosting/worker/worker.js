// packages.pyxis.nexus: read-only proxy for the Pyxis package repositories and
// install ISOs in the PyxisPackageRepo B2 bucket, whose object keys equal the
// URL paths (docs/interfaces/packages.md#repository). Integrity rests on the
// signed indexes and the signed SHA256SUMS, not on this proxy or TLS.

const ORIGIN = "https://f003.backblazeb2.com/file/PyxisPackageRepo";
const PATH_PATTERN = /^\/(public|releases|images)\/[A-Za-z0-9._+\/-]+$/;
const YEAR = 31536000;
const MISSING_SECONDS = 30;

// The published layout and how long each part may be cached: only current,
// SHA256SUMS and its signature change, and every other published file is
// written once.
const LAYOUT = [
  [/^\/(public|releases)\/current$/, 30],
  [/^\/(public|releases)\/signer[A-Za-z0-9._-]*\.pem$/, 3600],
  [/^\/(public|releases)\/index\/[0-9]+\.lua(\.sig)?$/, YEAR],
  [/^\/(public|releases)\/packages\/[^\/]+\/[^\/]+\.tar$/, YEAR],
  [/^\/images\/SHA256SUMS(\.sig)?$/, 30],
  [/^\/images\/(signer[A-Za-z0-9._-]*|pyxis-signing-ca)\.pem$/, 3600],
  [/^\/images\/pyxis-[0-9a-z][0-9a-z.-]*\.(iso|qcow2)$/, YEAR],
];

function cacheSeconds(path) {
  const match = LAYOUT.find(([pattern]) => pattern.test(path));
  return match ? match[1] : null;
}

function allowed(path) {
  return PATH_PATTERN.test(path) && !path.split("/").slice(1).some(
    (part) => part === "" || part === "." || part === "..");
}

export default {
  async fetch(request) {
    if (request.method !== "GET" && request.method !== "HEAD") {
      return new Response(null, { status: 405, headers: { Allow: "GET, HEAD" } });
    }
    const url = new URL(request.url);
    const seconds = allowed(url.pathname) ? cacheSeconds(url.pathname) : null;
    if (seconds === null || url.search !== "") {
      return new Response("not found\n", { status: 404 });
    }

    const forwarded = new Headers();
    for (const name of ["range", "if-none-match", "if-modified-since"]) {
      const value = request.headers.get(name);
      if (value !== null) {
        forwarded.set(name, value);
      }
    }
    // B2's download-by-name URLs decode "+" as a space, and archive names
    // carry "+" (ID/VARIANT+ABI.tar); only the upstream path is encoded.
    const origin = await fetch(ORIGIN + url.pathname.replaceAll("+", "%2B"), {
      method: request.method,
      headers: forwarded,
      cf: {
        cacheEverything: true,
        // A missed upload must not stick; origin failures are not cached.
        cacheTtlByStatus: { "200-299": seconds, "404": MISSING_SECONDS, "500-599": 0 },
      },
    });

    // The body streams through; archives never sit in Worker memory.
    const response = new Response(origin.body, origin);
    for (const name of [...response.headers.keys()]) {
      if (name.startsWith("x-bz-")) {
        response.headers.delete(name);
      }
    }
    if (origin.status >= 200 && origin.status < 300) {
      const immutable = seconds === YEAR ? ", immutable" : "";
      response.headers.set("Cache-Control", `public, max-age=${seconds}${immutable}`);
    } else if (origin.status === 404) {
      response.headers.set("Cache-Control", `public, max-age=${MISSING_SECONDS}`);
    } else {
      response.headers.set("Cache-Control", "no-store");
    }
    return response;
  },
};
