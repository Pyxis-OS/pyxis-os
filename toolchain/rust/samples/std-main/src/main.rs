use std::alloc::{Layout, alloc, alloc_zeroed, dealloc, realloc};
use std::collections::HashMap;
use std::env;
use std::ffi::{OsStr, OsString};
use std::hint::black_box;
use std::io::{self, IsTerminal, Read, Write};
use std::path::{Component, Path, PathBuf, absolute};
use std::process::ExitCode;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

unsafe extern "C" {
    fn startup_environment_handle() -> u64;
    fn environment_set_text(table: u64, name: *const libc::c_char,
                            text: *const libc::c_char, flags: u64, set_flags: u64) -> i32;
}

struct InitialThread(String);

impl Drop for InitialThread {
    fn drop(&mut self) {
        // stderr is unbuffered and libc drains this after Rust's process cleanup.
        let _ = writeln!(io::stderr(), "rust-std-main: initial TLS drop {}", self.0);
    }
}

thread_local! {
    static INITIAL: InitialThread = InitialThread(String::from("owned"));
}

fn owned_values(arguments: &[OsString]) {
    assert!(!arguments.is_empty());
    let copy = arguments.to_vec();
    assert_eq!(copy, env::args_os().collect::<Vec<_>>());
    let mut strings = vec![String::from("owned"), String::from("bytes")];
    strings[0].push_str(" argument");
    assert_eq!(strings.concat(), "owned argumentbytes");
    black_box((copy, strings));
}

fn allocation() {
    let layout = Layout::from_size_align(4096, 16384).unwrap();
    unsafe {
        let pointer = black_box(alloc_zeroed(black_box(layout)));
        assert!(!pointer.is_null(), "aligned allocation failed");
        assert_eq!(pointer.addr() % layout.align(), 0);
        assert!(std::slice::from_raw_parts(pointer, layout.size()).iter().all(|&b| b == 0));
        for index in 0..layout.size() {
            pointer.add(index).write((index % 251) as u8);
        }
        let grown = black_box(realloc(pointer, layout, 8192));
        assert!(!grown.is_null(), "aligned growth failed");
        assert_eq!(grown.addr() % layout.align(), 0);
        for index in 0..layout.size() {
            assert_eq!(grown.add(index).read(), (index % 251) as u8);
        }
        let large = Layout::from_size_align(8192, layout.align()).unwrap();
        let shrunk = black_box(realloc(grown, large, 2048));
        assert!(!shrunk.is_null(), "aligned shrink failed");
        assert_eq!(shrunk.addr() % layout.align(), 0);
        for index in 0..2048 {
            assert_eq!(shrunk.add(index).read(), (index % 251) as u8);
        }
        dealloc(shrunk, Layout::from_size_align(2048, layout.align()).unwrap());
    }
}

fn environment() {
    unsafe { env::set_var("rust.std.exported", "first"); }
    let bytes = unsafe { OsStr::from_encoded_bytes_unchecked(b"owned\xffTEXT") };
    unsafe { env::set_var("rust.std.bytes", bytes); }
    let status = unsafe {
        environment_set_text(startup_environment_handle(), c"rust.std.private".as_ptr(),
                             c"private first".as_ptr(), 0, 0)
    };
    assert_eq!(status, 0, "private TEXT publication failed");
    let exported = env::var_os("rust.std.exported").unwrap();
    let private = env::var_os("rust.std.private").unwrap();
    let snapshot: Vec<_> = env::vars_os().collect();
    assert!(snapshot.contains(&(OsString::from("rust.std.exported"), OsString::from("first"))));
    assert!(snapshot.contains(&(OsString::from("rust.std.private"), OsString::from("private first"))));
    assert_eq!(env::var_os("rust.std.bytes").unwrap().as_encoded_bytes(), bytes.as_encoded_bytes());
    assert!(snapshot.contains(&(OsString::from("rust.std.bytes"), bytes.to_owned())));
    for name in ["memory", "clock", "random"] {
        assert!(!snapshot.iter().any(|(key, _)| key == name), "CAP became TEXT");
        assert_eq!(env::var_os(name), None);
    }
    unsafe { env::set_var("rust.std.exported", "second"); }
    let status = unsafe {
        environment_set_text(startup_environment_handle(), c"rust.std.private".as_ptr(),
                             c"private second".as_ptr(), 0, 0)
    };
    assert_eq!(status, 0, "private TEXT replacement failed");
    assert_eq!(exported, "first");
    assert_eq!(private, "private first");
    assert!(snapshot.contains(&(OsString::from("rust.std.exported"), exported)));
    assert!(snapshot.contains(&(OsString::from("rust.std.private"), private)));
    assert_eq!(env::var("rust.std.exported").unwrap(), "second");
    assert_eq!(env::var("rust.std.private").unwrap(), "private second");
    unsafe {
        env::remove_var("rust.std.exported");
        env::remove_var("rust.std.private");
        env::remove_var("rust.std.bytes");
    }
    assert_eq!(env::var_os("rust.std.exported"), None);
    assert_eq!(env::var_os("rust.std.private"), None);
}

fn paths() -> io::Result<()> {
    let spelling = "sample://one//./two/../three";
    let path = Path::new(spelling);
    assert!(path.is_absolute() && path.has_root());
    assert_eq!(path.as_os_str().as_encoded_bytes(), spelling.as_bytes());
    assert_eq!(path.components().next(), Some(Component::SchemeRoot(OsStr::new("sample://"))));
    assert_eq!(path, Path::new("sample://one/two/../three"));
    assert_eq!(path.components().collect::<PathBuf>(), Path::new("sample://one/two/../three"));
    assert_eq!(path.parent(), Some(Path::new("sample://one/two/..")));
    assert_eq!(Path::new("sample://").parent(), None);
    assert!(!Path::new("/one").has_root());
    assert_eq!(absolute("/one").unwrap_err().kind(), io::ErrorKind::InvalidInput);
    assert_eq!(absolute(path)?.as_os_str().as_encoded_bytes(), spelling.as_bytes());
    assert_eq!(Path::new("sample://one").join("two"), Path::new("sample://one/two"));
    assert_eq!(Path::new("sample://one").join("other://two"), Path::new("other://two"));
    let mut lookup = HashMap::new();
    lookup.insert(path.to_path_buf(), "same components");
    assert_eq!(lookup.get(Path::new("sample://one/two/../three")), Some(&"same components"));
    let bytes = b"sample://raw\xff//./name";
    let raw = unsafe { OsStr::from_encoded_bytes_unchecked(bytes) };
    let stored = PathBuf::from(raw);
    assert_eq!(stored.as_os_str().as_encoded_bytes(), bytes);
    assert_eq!(absolute(&stored)?.as_os_str().as_encoded_bytes(), bytes);
    Ok(())
}

fn entropy() {
    let mut values = HashMap::new();
    values.insert(String::from("entropy"), 17);
    assert_eq!(values.get("entropy"), Some(&17));
    black_box(values);
}

fn clocks() -> io::Result<()> {
    let wall = SystemTime::now().duration_since(UNIX_EPOCH).map_err(io::Error::other)?;
    let start = Instant::now();
    std::thread::sleep(Duration::from_millis(2));
    let elapsed = start.elapsed();
    assert!(elapsed >= Duration::from_millis(2));
    writeln!(io::stdout(), "rust-std-main: clocks wall={wall:?} elapsed={elapsed:?}")
}

fn streams() -> io::Result<()> {
    let mut output = io::stdout().lock();
    writeln!(output, "rust-std-main: terminals stdin={} stdout={} stderr={}",
             io::stdin().is_terminal(), io::stdout().is_terminal(), io::stderr().is_terminal())?;
    output.write_all(b"rust-std-main: bytes \0\xff\n")?;
    let mut input = io::stdin().lock();
    let mut buffer = [0u8; 1024];
    loop {
        match input.read(&mut buffer) {
            Ok(0) => break,
            Ok(count) => output.write_all(&buffer[..count])?,
            Err(error) if error.kind() == io::ErrorKind::Interrupted => continue,
            Err(error) => return Err(error),
        }
    }
    output.flush()
}

fn allocation_failure() -> ExitCode {
    let layout = Layout::from_size_align(black_box(1usize << 60), 16384).unwrap();
    let pointer = black_box(unsafe { alloc(black_box(layout)) });
    if pointer.is_null() {
        let _ = writeln!(io::stderr(), "rust-std-main: allocation failed; exiting status=102");
        ExitCode::from(102)
    } else {
        unsafe { dealloc(pointer, layout); }
        let _ = writeln!(io::stderr(), "rust-std-main: oversized allocation unexpectedly succeeded");
        ExitCode::FAILURE
    }
}

fn run(arguments: &[OsString]) -> io::Result<()> {
    match arguments.get(1).and_then(|argument| argument.to_str()) {
        Some("--clock-only") => return clocks(),
        Some("--entropy-only") => {
            entropy();
            return writeln!(io::stdout(), "rust-std-main: entropy ok");
        }
        Some("--io") => return streams(),
        Some("--panic") => panic!("rust-std-main: requested panic"),
        Some(option) if option.starts_with("--") => {
            return Err(io::Error::other(format!("unknown option: {option}")));
        }
        _ => {}
    }
    INITIAL.with(|value| assert_eq!(value.0, "owned"));
    owned_values(arguments);
    allocation();
    environment();
    paths()?;
    entropy();
    clocks()?;
    writeln!(io::stdout(), "rust-std-main: ok args={arguments:?}")
}

fn main() -> ExitCode {
    let arguments: Vec<_> = env::args_os().collect();
    if arguments.get(1).is_some_and(|argument| argument == "--allocation-failure") {
        return allocation_failure();
    }
    match run(&arguments).and_then(|()| io::stdout().flush()) {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            let _ = writeln!(io::stderr(), "rust-std-main: {error} (kind={:?}, raw={:?})",
                             error.kind(), error.raw_os_error());
            ExitCode::FAILURE
        }
    }
}
