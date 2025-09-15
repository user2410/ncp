use std::path::Path;
use std::io;

use crate::types::Result;

/// Get available disk space for a given path
pub fn get_available_space<P: AsRef<Path>>(path: P) -> Result<u64> {
    let path = path.as_ref();
    
    // If path doesn't exist, check its parent directory
    let check_path = if path.exists() {
        path
    } else {
        let parent = path.parent().unwrap_or_else(|| Path::new(""));

        if parent.as_os_str().is_empty() {
            Path::new(".") // Use current directory instead of empty string
        } else {
            parent
        }
    };
    
    #[cfg(unix)]
    {
        get_available_space_unix(check_path)
    }
    
    #[cfg(windows)]
    {
        get_available_space_windows(check_path)
    }
    
    #[cfg(not(any(unix, windows)))]
    {
        // Fallback for other platforms
        Ok(u64::MAX)
    }
}

#[cfg(unix)]
fn get_available_space_unix(path: &Path) -> Result<u64> {
    use std::ffi::CString;
    use std::mem;
    use std::os::raw::{c_char, c_int, c_ulong};
    
    #[repr(C)]
    struct Statvfs {
        f_bsize: c_ulong,    // Filesystem block size
        f_frsize: c_ulong,   // Fragment size
        f_blocks: c_ulong,   // Size of fs in f_frsize units
        f_bfree: c_ulong,    // Number of free blocks
        f_bavail: c_ulong,   // Number of free blocks for unprivileged users
        f_files: c_ulong,    // Number of inodes
        f_ffree: c_ulong,    // Number of free inodes
        f_favail: c_ulong,   // Number of free inodes for unprivileged users
        f_fsid: c_ulong,     // Filesystem ID
        f_flag: c_ulong,     // Mount flags
        f_namemax: c_ulong,  // Maximum filename length
    }
    
    unsafe extern "C" {
        unsafe fn statvfs(path: *const c_char, buf: *mut Statvfs) -> c_int;
    }
    
    let path_cstring = CString::new(path.to_string_lossy().as_bytes())
        .map_err(|_| "Invalid path for disk space check")?;
    
    let mut stat: Statvfs = unsafe { mem::zeroed() };
    
    let result = unsafe { statvfs(path_cstring.as_ptr(), &mut stat) };
    
    if result != 0 {
        return Err(io::Error::last_os_error().into());
    }
    
    // Available space = available blocks * block size
    // Use checked multiplication to avoid overflow
    let available_space = (stat.f_bavail as u64)
        .checked_mul(stat.f_frsize as u64)
        .unwrap_or(u64::MAX);
    Ok(available_space)
}

#[cfg(windows)]
fn get_available_space_windows(path: &Path) -> Result<u64> {
    use std::ffi::OsStr;
    use std::iter;
    use std::os::windows::ffi::OsStrExt;
    use std::ptr;
    
    // Convert path to wide string
    let wide: Vec<u16> = OsStr::new(path)
        .encode_wide()
        .chain(iter::once(0))
        .collect();
    
    let mut free_bytes_available = 0u64;
    let mut total_number_of_bytes = 0u64;
    let mut total_number_of_free_bytes = 0u64;
    
    extern "system" {
        fn GetDiskFreeSpaceExW(
            lp_directory_name: *const u16,
            lp_free_bytes_available_to_caller: *mut u64,
            lp_total_number_of_bytes: *mut u64,
            lp_total_number_of_free_bytes: *mut u64,
        ) -> i32;
    }
    
    let result = unsafe {
        GetDiskFreeSpaceExW(
            wide.as_ptr(),
            &mut free_bytes_available,
            &mut total_number_of_bytes,
            &mut total_number_of_free_bytes,
        )
    };
    
    if result == 0 {
        return Err(io::Error::last_os_error().into());
    }
    
    Ok(free_bytes_available)
}

/// Check if there's enough disk space for a file transfer
pub fn check_disk_space<P: AsRef<Path>>(path: P, required_bytes: u64) -> Result<bool> {
    let available = get_available_space(path)?;
    
    // Add 10% buffer for safety, using checked arithmetic to avoid overflow
    let buffer = required_bytes / 10;
    let required_with_buffer = required_bytes.checked_add(buffer).unwrap_or(u64::MAX);
    
    Ok(available >= required_with_buffer)
}

/// Format bytes in human-readable format
pub fn format_bytes(bytes: u64) -> String {
    const UNITS: &[&str] = &["B", "KB", "MB", "GB", "TB"];
    const THRESHOLD: u64 = 1024;
    
    if bytes < THRESHOLD {
        return format!("{} B", bytes);
    }
    
    let mut size = bytes as f64;
    let mut unit_index = 0;
    
    while size >= THRESHOLD as f64 && unit_index < UNITS.len() - 1 {
        size /= THRESHOLD as f64;
        unit_index += 1;
    }
    
    format!("{:.1} {}", size, UNITS[unit_index])
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::env;
    
    #[test]
    fn test_get_available_space() {
        let temp_dir = env::temp_dir();
        let space = get_available_space(&temp_dir).unwrap();
        
        // Should have some available space
        assert!(space > 0);
        println!("Available space in {:?}: {} bytes", temp_dir, space);
    }
    
    #[test]
    fn test_check_disk_space() {
        let temp_dir = env::temp_dir();
        
        // Should have space for a small file
        assert!(check_disk_space(&temp_dir, 1024).unwrap());
        
        // Test with a very large file size that should exceed available space
        // Get actual available space first
        let available = get_available_space(&temp_dir).unwrap();
        if available < u64::MAX / 2 {
            // Only test if available space is reasonable (not u64::MAX)
            let too_large = available * 2; // Request twice the available space
            let result = check_disk_space(&temp_dir, too_large);
            if let Ok(has_space) = result {
                assert!(!has_space, "Should not have space for {} bytes when only {} available", too_large, available);
            }
        }
    }
    
    #[test]
    fn test_format_bytes() {
        assert_eq!(format_bytes(0), "0 B");
        assert_eq!(format_bytes(512), "512 B");
        assert_eq!(format_bytes(1024), "1.0 KB");
        assert_eq!(format_bytes(1536), "1.5 KB");
        assert_eq!(format_bytes(1024 * 1024), "1.0 MB");
        assert_eq!(format_bytes(1024 * 1024 * 1024), "1.0 GB");
    }
    
    #[test]
    fn test_nonexistent_path() {
        let nonexistent = Path::new("/tmp/nonexistent/deep/path");
        
        // Should check parent directory that exists
        let result = get_available_space(nonexistent);
        
        // Should either succeed (checking parent) or fail gracefully
        match result {
            Ok(space) => {
                println!("Space for nonexistent path: {}", space);
                // u64 is always >= 0, so no need to check
            }
            Err(e) => {
                println!("Expected error for nonexistent path: {}", e);
            }
        }
    }
}
