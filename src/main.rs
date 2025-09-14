use std::env;
use std::path::PathBuf;
use std::process;
use std::sync::atomic::{AtomicU8, Ordering};

mod directory;
mod diskspace;
mod protocol;
mod recv;
mod send;
mod types;

// Global verbosity level
static VERBOSITY: AtomicU8 = AtomicU8::new(0);

// Minimal logging macros
macro_rules! vlog {
    (1, $($arg:tt)*) => {
        if crate::VERBOSITY.load(std::sync::atomic::Ordering::Relaxed) >= 1 {
            eprintln!("[INFO] {}", format!($($arg)*));
        }
    };
    (2, $($arg:tt)*) => {
        if crate::VERBOSITY.load(std::sync::atomic::Ordering::Relaxed) >= 2 {
            eprintln!("[DEBUG] {}", format!($($arg)*));
        }
    };
}

pub(crate) use vlog;

#[derive(Clone)]
enum OverwriteMode {
    Ask,
    Yes,
    No,
}

struct Args {
    verbose: u8,
    command: Command,
}

enum Command {
    Send {
        host: Option<String>,
        port: u16,
        retries: u32,
        overwrite: OverwriteMode,
        listen: bool,
        src: PathBuf,
    },
    Recv {
        host: String,
        port: u16,
        overwrite: OverwriteMode,
        dst: PathBuf,
    },
}

fn parse_args() -> Result<Args, String> {
    let args: Vec<String> = env::args().collect();
    
    if args.len() < 2 {
        return Err("Usage: ncp [send|recv] [options]".to_string());
    }
    
    let mut verbose = 0;
    let mut i = 1;
    
    while i < args.len() && args[i].starts_with('-') && args[i] != "--" {
        match args[i].as_str() {
            "-v" => verbose = 1,
            "-vv" => verbose = 2,
            "--help" | "-h" => {
                print_help();
                process::exit(0);
            }
            _ => break,
        }
        i += 1;
    }
    
    if i >= args.len() {
        return Err("Missing command".to_string());
    }
    
    let command = match args[i].as_str() {
        "send" => parse_send_args(&args[i+1..])?,
        "recv" => parse_recv_args(&args[i+1..])?,
        _ => return Err(format!("Unknown command: {}", args[i])),
    };
    
    Ok(Args { verbose, command })
}

fn parse_send_args(args: &[String]) -> Result<Command, String> {
    let mut host = None;
    let mut port = None;
    let mut retries = 3;
    let mut overwrite = OverwriteMode::Ask;
    let mut listen = false;
    let mut src = None;
    
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--host" => {
                i += 1;
                if i >= args.len() { return Err("--host requires value".to_string()); }
                host = Some(args[i].clone());
            }
            "--port" => {
                i += 1;
                if i >= args.len() { return Err("--port requires value".to_string()); }
                port = Some(args[i].parse().map_err(|_| "Invalid port".to_string())?);
            }
            "--retries" => {
                i += 1;
                if i >= args.len() { return Err("--retries requires value".to_string()); }
                retries = args[i].parse().map_err(|_| "Invalid retries".to_string())?;
            }
            "--overwrite" => {
                i += 1;
                if i >= args.len() { return Err("--overwrite requires value".to_string()); }
                overwrite = match args[i].as_str() {
                    "ask" => OverwriteMode::Ask,
                    "yes" => OverwriteMode::Yes,
                    "no" => OverwriteMode::No,
                    _ => return Err("Invalid overwrite mode".to_string()),
                };
            }
            "--listen" | "-l" => {
                listen = true;
            }
            arg if !arg.starts_with('-') => {
                src = Some(PathBuf::from(arg));
            }
            _ => return Err(format!("Unknown option: {}", args[i])),
        }
        i += 1;
    }
    
    if !listen && host.is_none() {
        return Err("--host required (or use --listen)".to_string());
    }
    
    Ok(Command::Send {
        host,
        port: port.ok_or("--port required")?,
        retries,
        overwrite,
        listen,
        src: src.ok_or("source path required")?,
    })
}

fn parse_recv_args(args: &[String]) -> Result<Command, String> {
    let mut host = "0.0.0.0".to_string();
    let mut port = None;
    let mut overwrite = OverwriteMode::Ask;
    let mut dst = None;
    
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--host" => {
                i += 1;
                if i >= args.len() { return Err("--host requires value".to_string()); }
                host = args[i].clone();
            }
            "--port" => {
                i += 1;
                if i >= args.len() { return Err("--port requires value".to_string()); }
                port = Some(args[i].parse().map_err(|_| "Invalid port".to_string())?);
            }
            "--overwrite" => {
                i += 1;
                if i >= args.len() { return Err("--overwrite requires value".to_string()); }
                overwrite = match args[i].as_str() {
                    "ask" => OverwriteMode::Ask,
                    "yes" => OverwriteMode::Yes,
                    "no" => OverwriteMode::No,
                    _ => return Err("Invalid overwrite mode".to_string()),
                };
            }
            arg if !arg.starts_with('-') => {
                dst = Some(PathBuf::from(arg));
            }
            _ => return Err(format!("Unknown option: {}", args[i])),
        }
        i += 1;
    }
    
    Ok(Command::Recv {
        host,
        port: port.ok_or("--port required")?,
        overwrite,
        dst: dst.ok_or("destination path required")?,
    })
}

fn print_help() {
    println!("ncp {} - Minimal file transfer over TCP", env!("CARGO_PKG_VERSION"));
    println!();
    println!("USAGE:");
    println!("    ncp [-v|-vv] send --host <HOST> --port <PORT> [OPTIONS] <SRC>");
    println!("    ncp [-v|-vv] send --listen --port <PORT> [OPTIONS] <SRC>");
    println!("    ncp [-v|-vv] recv --port <PORT> [OPTIONS] <DST>");
    println!();
    println!("OPTIONS:");
    println!("    -v, -vv          Increase verbosity");
    println!("    --host <HOST>    Target host (required for send without --listen)");
    println!("    --port <PORT>    Port number");
    println!("    --listen, -l     Listen mode (send only)");
    println!("    --retries <N>    Retry attempts (send only, default: 3)");
    println!("    --overwrite <M>  Overwrite mode: ask, yes, no (default: ask)");
    println!("    -h, --help       Show this help");
}

fn main() {
    let args = match parse_args() {
        Ok(args) => args,
        Err(e) => {
            eprintln!("Error: {}", e);
            process::exit(1);
        }
    };
    
    VERBOSITY.store(args.verbose, Ordering::Relaxed);
    vlog!(1, "Starting ncp with verbosity level {}", args.verbose);

    let result = match args.command {
        Command::Send { host, port, retries, overwrite, listen, src } => {
            if listen {
                vlog!(2, "Executing send listen command: port {} -> {:?}", port, src);
                send::execute_listen(port, src, overwrite)
            } else {
                let host = host.unwrap();
                vlog!(2, "Executing send command: {}:{} -> {:?}", host, port, src);
                send::execute(host, port, src, retries, overwrite)
            }
        }
        Command::Recv { host, port, overwrite, dst } => {
            vlog!(2, "Executing recv command: {}:{} -> {:?}", host, port, dst);
            recv::execute(host, port, dst, overwrite)
        }
    };

    match result {
        Ok(()) => {
            vlog!(1, "Operation completed successfully");
            process::exit(0);
        }
        Err(e) => {
            eprintln!("Error: {}", e);
            vlog!(2, "Operation failed with error: {}", e);
            process::exit(1);
        }
    }
}