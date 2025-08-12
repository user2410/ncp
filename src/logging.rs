use std::sync::atomic::{AtomicU8, Ordering};

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
    (3, $($arg:tt)*) => {
        if crate::VERBOSITY.load(std::sync::atomic::Ordering::Relaxed) >= 3 {
            eprintln!("[TRACE] {}", format!($($arg)*));
        }
    };
}

pub(crate) use vlog;
