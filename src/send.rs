use crate::protocol::*;
use crate::directory::{walk_directory, calculate_total_size};
use crate::{OverwriteMode, vlog};
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
    overwrite_mode: OverwriteMode,
) -> Result<()> {
    if !src.exists() {
        return Err(format!("Source path does not exist: {}", src.display()).into());
    }

    let is_directory = src.is_dir();
    vlog!(2, "Source is {}: {:?}", if is_directory { "directory" } else { "file" }, src);

    let mut last_error = None;
    
    for attempt in 1..=retries {
        println!("Attempt {}/{}", attempt, retries);
        
        match attempt_transfer(&host, port, &src, &overwrite_mode, is_directory) {
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
    _overwrite_mode: &OverwriteMode,
    is_directory: bool,
) -> Result<()> {
    println!("Connecting to {}:{}...", host, port);
    vlog!(2, "Attempting TCP connection to {}:{}", host, port);
    let mut stream = TcpStream::connect((host, port))?;
    
    println!("Connection established");
    vlog!(2, "Connection established");

    if is_directory {
        transfer_directory(&mut stream, src_path)?;
    } else {
        transfer_single_file(&mut stream, src_path)?;
    }

    Ok(())
}

fn transfer_directory(
    stream: &mut TcpStream,
    src_path: &Path,
) -> Result<()> {
    let entries = walk_directory(src_path)?;
    let total_size = calculate_total_size(&entries);
    
    vlog!(1, "Directory contains {} entries, total size: {} bytes", entries.len(), total_size);
    
    for entry in entries {
        vlog!(2, "Transferring {}: {:?}", if entry.is_dir { "directory" } else { "file" }, entry.relative_path);
        
        let file_meta = FileMeta {
            name: entry.relative_path.to_string_lossy().to_string(),
            size: entry.size,
            is_dir: entry.is_dir,
        };
        
        write_meta(stream, &file_meta)?;
        wait_for_preflight(stream)?;
        
        if !entry.is_dir {
            transfer_file_data(stream, &entry.path, entry.size)?;
        }
    }
    
    Ok(())
}

fn transfer_single_file(
    stream: &mut TcpStream,
    src_path: &Path,
) -> Result<()> {
    let file_size = std::fs::metadata(src_path)?.len();
    let file_name = src_path.file_name()
        .ok_or("Invalid filename")?
        .to_string_lossy()
        .to_string();

    let file_meta = FileMeta {
        name: file_name,
        size: file_size,
        is_dir: false,
    };

    write_meta(stream, &file_meta)?;
    wait_for_preflight(stream)?;
    transfer_file_data(stream, src_path, file_size)?;
    
    Ok(())
}

fn wait_for_preflight(stream: &mut TcpStream) -> Result<()> {
    let msg_type = read_message_type(stream)?;
    let _len = read_message_length(stream)?;
    
    match msg_type {
        MSG_PREFLIGHT_OK => {
            let _preflight_ok = read_preflight_ok(stream)?;
            vlog!(2, "Preflight check passed");
            Ok(())
        }
        MSG_PREFLIGHT_FAIL => {
            let preflight_fail = read_preflight_fail(stream)?;
            Err(preflight_fail.reason.into())
        }
        _ => Err("Unexpected response to Meta message".into()),
    }
}

fn transfer_file_data(
    stream: &mut TcpStream,
    file_path: &Path,
    file_size: u64,
) -> Result<()> {
    let transfer_start = TransferStart { file_size };
    write_transfer_start(stream, &transfer_start)?;

    let file = File::open(file_path)?;
    let mut reader = BufReader::new(file);
    let mut buffer = [0u8; 8192];
    let mut total_sent = 0u64;

    vlog!(2, "Starting file data transfer: {} bytes", file_size);
    
    loop {
        let bytes_read = reader.read(&mut buffer)?;
        if bytes_read == 0 {
            break;
        }
        
        write_raw_bytes(stream, &buffer[..bytes_read])?;
        total_sent += bytes_read as u64;
        
        if total_sent % (1024 * 1024) == 0 || total_sent == file_size {
            print!("\rSent: {}/{} bytes", total_sent, file_size);
            stdout().flush().unwrap();
        }
    }
    println!();

    if total_sent != file_size {
        return Err(format!("File size mismatch: sent {} bytes, expected {}", total_sent, file_size).into());
    }

    // Wait for TransferResult
    let msg_type = read_message_type(stream)?;
    let _len = read_message_length(stream)?;
    
    if msg_type != MSG_TRANSFER_RESULT {
        return Err("Expected TransferResult message".into());
    }
    
    let transfer_result = read_transfer_result(stream)?;
    
    if transfer_result.ok {
        vlog!(2, "File transfer successful: {} bytes", transfer_result.received_bytes);
    } else {
        return Err("Transfer failed".into());
    }

    Ok(())
}