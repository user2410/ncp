use clap::{Parser, Subcommand, ValueEnum};
use std::path::PathBuf;
use std::process;
use std::sync::atomic::{AtomicU8, Ordering};

mod checksum;
mod directory;
mod diskspace;
mod framing;
mod proto;
mod recv;
mod send;
mod types;
mod hostname;

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

#[derive(Parser)]
#[command(name = "ncp")]
#[command(about = "Minimal file transfer over TCP")]
#[command(version = env!("CARGO_PKG_VERSION"))]
struct Cli {
    /// Increase verbosity (-v info, -vv debug, -vvv trace)
    #[arg(short, long, action = clap::ArgAction::Count)]
    verbose: u8,
    
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    Send {
        #[arg(long, required = true)]
        host: String,
        
        #[arg(long, required = true)]
        port: u16,
        
        #[arg(long, default_value_t = 3)]
        retries: u32,
        
        #[arg(long, value_enum, default_value_t = ChecksumMode::Hash)]
        checksum: ChecksumMode,
        
        #[arg(long, value_enum, default_value_t = OverwriteMode::Ask)]
        overwrite: OverwriteMode,
        
        /// Source file or directory
        src: PathBuf,
    },
    Recv {
        #[arg(long, default_value_t = String::from("0.0.0.0"))]
        host: String,

        #[arg(long, required = true)]
        port: u16,
        
        #[arg(long, value_enum, default_value_t = ChecksumMode::Hash)]
        checksum: ChecksumMode,
        
        #[arg(long, value_enum, default_value_t = OverwriteMode::Ask)]
        overwrite: OverwriteMode,
        
        /// Destination file or directory
        dst: PathBuf,
    },
}

#[derive(Clone, ValueEnum)]
enum ChecksumMode {
    Hash,
    None,
}

#[derive(Clone, ValueEnum)]
enum OverwriteMode {
    Ask,
    Yes,
    No,
}

#[derive(Debug)]
pub enum ExitCode {
    Success = 0,
    GeneralError = 1,
    ProtocolError = 2,
    IoError = 3,
    PermissionDenied = 4,
    ChecksumMismatch = 5,
    NoSpace = 6,
    MaxRetriesExceeded = 11,
}

impl ExitCode {
    pub fn exit(self) -> ! {
        process::exit(self as i32);
    }
}

fn main() {
    let cli = Cli::parse();
    
    // Set global verbosity level
    VERBOSITY.store(cli.verbose, Ordering::Relaxed);
    
    vlog!(1, "Starting ncp with verbosity level {}", cli.verbose);

    let result = match cli.command {
        Commands::Send { host, port, retries, checksum, overwrite, src } => {
            vlog!(2, "Executing send command: {}:{} -> {:?}", host, port, src);
            send::execute(host, port, src, retries, checksum, overwrite)
        }
        Commands::Recv { host, port, checksum, overwrite, dst } => {
            vlog!(2, "Executing recv command: {}:{} -> {:?}", host, port, dst);
            recv::execute(host, port, dst, checksum, overwrite)
        }
    };

    match result {
        Ok(()) => {
            vlog!(1, "Operation completed successfully");
            ExitCode::Success.exit();
        }
        Err(e) => {
            eprintln!("Error: {}", e);
            vlog!(2, "Operation failed with error: {}", e);
            ExitCode::GeneralError.exit();
        }
    }
}
