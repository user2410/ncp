use crate::proto::*;
use prost::Message;
use crate::framing::{read_message, write_message, write_exact_bytes};
use crate::checksum::calculate_file_checksum;
use crate::directory::{walk_directory, calculate_total_size};
use crate::{ChecksumMode, OverwriteMode, vlog};
use crate::types::Result;

use std::fs::File;
use std::io::{Read, BufReader, stdout, Write};
use std::net::TcpStream;
use std::path::{Path, PathBuf};
use std::time::Duration;

pub fn execute(
    host: String,
    port: u16,
    src: PathBuf,
    retries: u32,
    checksum_mode: ChecksumMode,
    overwrite_mode: OverwriteMode,
) -> Result<()> {
    // Validate source path
    if !src.exists() {
        return Err(format!("Source path does not exist: {}", src.display()).into());
    }

    let is_directory = src.is_dir();
    vlog!(2, "Source is {}: {:?}", if is_directory { "directory" } else { "file" }, src);

    let mut last_error = None;
    
    for attempt in 1..=retries {
        println!("Attempt {}/{}", attempt, retries);
        
        match attempt_transfer(&host, port, &src, &checksum_mode, &overwrite_mode, is_directory) {
            Ok(()) => {
                println!("Transfer completed successfully");
                return Ok(());
            }
            Err(e) => {
                eprintln!("Attempt {} failed: {}", attempt, e);
                last_error = Some(e);
                
                if attempt < retries {
                    println!("Retrying in 1 second...");
                    std::thread::sleep(Duration::from_secs(1));
                }
            }
        }
    }

    if let Some(e) = last_error {
        Err(format!("All {} attempts failed. Last error: {}", retries, e).into())
    } else {
        Err("Transfer failed".into())
    }
}

fn attempt_transfer(
    host: &str,
    port: u16,
    src_path: &Path,
    checksum_mode: &ChecksumMode,
    _overwrite_mode: &OverwriteMode,
    is_directory: bool,
) -> Result<()> {
    println!("Connecting to {}:{}...", host, port);
    vlog!(2, "Attempting TCP connection to {}:{}", host, port);
    let mut stream = TcpStream::connect((host, port))?;
    vlog!(2, "TCP connection established");
    
    // Generate session ID
    let session_id = format!("session_{}", std::process::id());
    
    // Send Probe message
    let probe = Probe::new(
        session_id.clone(),
        "0.1.0".to_string(),
        "ncp-minimal".to_string(),
    );
    vlog!(2, "Sending Probe message with session_id: {}", session_id);
    write_message(&mut stream, &probe)?;
    
    // Wait for Established response
    let established: Established = read_message(&mut stream)?;
    if established.session_id != session_id {
        return Err("Session ID mismatch".into());
    }
    println!("Connection established");
    vlog!(2, "Connection established with session_id: {}", session_id);

    // Get file metadata
    let file_size = std::fs::metadata(src_path)?.len();
    let file_name = src_path.file_name()
        .ok_or("Invalid filename")?
        .to_string_lossy()
        .to_string();

    // Calculate checksum if enabled
    let checksum = if matches!(checksum_mode, ChecksumMode::Hash) {
        vlog!(2, "Calculating checksum for file: {:?}", src_path);
        let checksum = calculate_file_checksum(src_path)?;
        vlog!(2, "Checksum calculated: {} bytes", checksum.len());
        checksum
    } else {
        vlog!(2, "Checksum disabled");
        vec![]
    };

    // Create and send Meta message
    let mut file_meta = FileMeta::new(file_name, file_size, false);
    file_meta.checksum = checksum;
    file_meta.checksum_alg = if matches!(checksum_mode, ChecksumMode::Hash) {
        "defaulthash".to_string()
    } else {
        "none".to_string()
    };

    let meta = Meta::new(session_id.clone(), file_meta);
    write_message(&mut stream, &meta)?;

    // Wait for preflight response
    loop {
        // Try to read different message types
        let mut buffer = Vec::new();
        let mut len_buf = [0u8; 4];
        stream.read_exact(&mut len_buf)?;
        let len = u32::from_be_bytes(len_buf);
        
        if len > 1024 * 1024 {
            return Err("Message too large".into());
        }
        
        buffer.resize(len as usize, 0);
        stream.read_exact(&mut buffer)?;
        
        // Try to decode as PreflightOk first
        if let Ok(_) = PreflightOk::decode(&buffer[..]) {
            vlog!(1, "Preflight check passed");
            break;
        }
        
        // Try to decode as PreflightFail
        if let Ok(preflight_fail) = PreflightFail::decode(&buffer[..]) {
            let error_msg = if preflight_fail.reason.is_empty() {
                format!("Preflight failed with code: {}", preflight_fail.code)
            } else {
                preflight_fail.reason
            };
            return Err(error_msg.into());
        }
        
        return Err("Unexpected response to Meta message".into());
    }

    // Send TransferStart message
    let transfer_start = TransferStart::new(session_id.clone(), file_size);
    write_message(&mut stream, &transfer_start)?;

    // Open source file and start streaming
    let file = File::open(src_path)?;
    let mut reader = BufReader::new(file);
    let mut buffer = [0u8; 8192];
    let mut total_sent = 0u64;

    vlog!(1, "Sending file data...");
    vlog!(2, "Starting file data transfer: {} bytes", file_size);
    
    loop {
        let bytes_read = reader.read(&mut buffer)?;
        if bytes_read == 0 {
            break;
        }
        
        write_exact_bytes(&mut stream, &buffer[..bytes_read])?;
        total_sent += bytes_read as u64;
        
        // Simple progress indicator
        if total_sent % (1024 * 1024) == 0 || total_sent == file_size {
            print!("\rSent: {}/{} bytes", total_sent, file_size);
            stdout().flush().unwrap();
        }
    }
    println!(); // New line after progress

    if total_sent != file_size {
        return Err(format!("File size mismatch: sent {} bytes, expected {}", total_sent, file_size).into());
    }

    // Wait for TransferResult
    let transfer_result: TransferResult = read_message(&mut stream)?;
    
    if transfer_result.ok {
        vlog!(1, "Transfer successful!");
        if transfer_result.received_bytes != file_size {
            vlog!(2, "Warning: Received bytes ({}) != sent bytes ({})", 
                     transfer_result.received_bytes, file_size);
        }
    } else {
        let error_msg = if transfer_result.reason.is_empty() {
            format!("Transfer failed with code: {}", transfer_result.code)
        } else {
            transfer_result.reason
        };
        return Err(error_msg.into());
    }

    Ok(())
}
