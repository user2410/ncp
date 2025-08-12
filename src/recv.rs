use crate::proto::*;
use crate::diskspace::{check_disk_space, format_bytes};
use crate::framing::{read_message, write_message, read_exact_bytes};
use crate::checksum::StreamingChecksum;
use crate::types::Result;
use crate::{ChecksumMode, OverwriteMode, vlog};

use std::fs::{self, File};
use std::io::{Write, BufWriter, stdin, stdout};
use std::net::{TcpListener, TcpStream};
use std::path::{Path, PathBuf};

pub fn execute(
    host: String,
    port: u16,
    dst: PathBuf,
    checksum_mode: ChecksumMode,
    overwrite_mode: OverwriteMode,
) -> Result<()> {
    let listener = TcpListener::bind((host.clone(), port))?;
    println!("Listening on port {}", port);
    vlog!(2, "TCP listener bound to {}:{}", host, port);

    for stream in listener.incoming() {
        let stream = stream?;
        let peer_addr = stream.peer_addr()?;
        println!("Connection from: {}", peer_addr);
        vlog!(2, "Accepted connection from: {}", peer_addr);
        
        match handle_connection(stream, &dst, checksum_mode.clone(), overwrite_mode.clone()) {
            Ok(()) => {
                println!("Transfer completed successfully");
                break;
            }
            Err(e) => {
                eprintln!("Transfer failed");
                vlog!(2, "{}", e);
                // For minimal version, we exit after first attempt
                return Err(e);
            }
        }
    }

    Ok(())
}

fn handle_connection(
    mut stream: TcpStream,
    dst_path: &Path,
    checksum_mode: ChecksumMode,
    overwrite_mode: OverwriteMode,
) -> Result<()> {
    let session_id: String;

    // Wait for Probe message
    loop {
        let probe: Probe = read_message(&mut stream)?;
        session_id = probe.session_id.clone();
        println!("Received probe from client: {}", probe.client_name);
        vlog!(2, "Probe received: session_id={}, client={}", session_id, probe.client_name);
        
        // Send Established response
        let established = Established::new(session_id.clone(), "0.1.0".to_string());
        write_message(&mut stream, &established)?;
        break;
    }

    // Wait for Meta message
    let meta: Meta = read_message(&mut stream)?;
    let file_meta = meta.file.ok_or("Missing file metadata")?;

    // Determine final destination path
    let final_path = determine_final_path(dst_path, &file_meta.name, file_meta.is_dir)?;
    
    vlog!(2, "Receiving file: {} ({}) to {}", 
             file_meta.name, 
             format_bytes(file_meta.size),
             final_path.display());

    // Check for overwrite conflicts
    if final_path.exists() {
        match overwrite_mode {
            OverwriteMode::Ask => {
                if !prompt_overwrite(&final_path)? {
                    let preflight_fail = PreflightFail::new(
                        session_id.clone(),
                        ErrorCode::ErrPermission,
                        "User declined overwrite".to_string(),
                    );
                    write_message(&mut stream, &preflight_fail)?;
                    return Ok(());
                }
            }
            OverwriteMode::No => {
                println!("File exists, skipping: {}", final_path.display());
                let preflight_fail = PreflightFail::new(
                    session_id.clone(),
                    ErrorCode::ErrPermission,
                    "File exists, skipping".to_string(),
                );
                write_message(&mut stream, &preflight_fail)?;
                return Ok(());
            }
            OverwriteMode::Yes => {
                // Continue with transfer
            }
        }
    }

    // Create parent directory if needed
    if let Some(parent) = final_path.parent() {
        fs::create_dir_all(parent)?;
    }

    // Check disk space availability
    let available_space = get_available_space(&final_path)?;

    println!("Available disk space: {}", format_bytes(available_space));
    vlog!(2, "Available disk space: {} bytes", available_space);

    let has_enough_space = check_disk_space(&final_path, file_meta.size)?;
    
    if !has_enough_space {
        let error_msg = format!(
            "Insufficient disk space. Need: {}, Available: {}",
            format_bytes(file_meta.size),
            format_bytes(available_space)
        );
        println!("{}", error_msg);
        
        let preflight_fail = PreflightFail::new(
            session_id.clone(),
            ErrorCode::ErrNoSpace,
            error_msg,
        );
        write_message(&mut stream, &preflight_fail)?;
        return Err("Insufficient disk space".into());
    }

    // Send PreflightOk
    let preflight_ok = PreflightOk::new(
        session_id.clone(),
        final_path.exists(),
        available_space,
    );
    write_message(&mut stream, &preflight_ok)?;

    // Wait for TransferStart
    let transfer_start: TransferStart = read_message(&mut stream)?;
    
    // Create temporary file for atomic write
    let temp_path = final_path.with_extension("ncp_temp");
    let temp_file = File::create(&temp_path)?;
    let mut writer = BufWriter::new(temp_file);
    
    // Stream file data
    let mut total_bytes = 0u64;
    let mut checksum_calc = StreamingChecksum::new();
    let mut buffer = [0u8; 8192];
    let file_size = transfer_start.file_size;
    
    while total_bytes < file_size {
        let remaining = (file_size - total_bytes) as usize;
        let to_read = remaining.min(buffer.len());
        
        read_exact_bytes(&mut stream, &mut buffer[..to_read])?;
        writer.write_all(&buffer[..to_read])?;
        
        if matches!(checksum_mode, ChecksumMode::Hash) {
            checksum_calc.update(&buffer[..to_read]);
        }
        
        total_bytes += to_read as u64;
        
        // Simple progress indicator
        if total_bytes % (1024 * 1024) == 0 || total_bytes == file_size {
            print!("\rReceived: {}/{} bytes", total_bytes, file_size);
            stdout().flush().unwrap();
        }
    }
    println!(); // New line after progress
    
    writer.flush()?;
    drop(writer);

    // Verify checksum if enabled
    let checksum_match = if matches!(checksum_mode, ChecksumMode::Hash) {
        let calculated_checksum = checksum_calc.finalize();
        let expected_checksum = &file_meta.checksum;
        
        expected_checksum.is_empty() || (calculated_checksum == *expected_checksum)
    } else {
        true // Checksum verification disabled
    };

    let transfer_result = if checksum_match {
        // Atomically move temp file to final location
        fs::rename(&temp_path, &final_path)?;
        println!("File saved to: {}", final_path.display());
        vlog!(1, "File successfully saved to: {}", final_path.display());
        
        TransferResult::new(session_id.clone(), true, total_bytes)
    } else {
        // Remove temp file on checksum failure
        let _ = fs::remove_file(&temp_path);
        println!("Checksum verification failed");
        vlog!(2, "Checksum verification failed for file: {}", final_path.display());
        
        let mut result = TransferResult::new(session_id.clone(), false, total_bytes);
        result.code = ErrorCode::ErrChecksum as i32;
        result.reason = "Checksum mismatch".to_string();
        result
    };

    write_message(&mut stream, &transfer_result)?;

    if !checksum_match {
        return Err("Checksum verification failed".into());
    }

    Ok(())
}

fn determine_final_path(dst_path: &Path, file_name: &str, is_dir: bool) -> Result<PathBuf> {
    if dst_path.is_dir() {
        // dst is directory, put file inside it
        Ok(dst_path.join(file_name))
    } else if dst_path.exists() {
        // dst is existing file
        if is_dir {
            return Err("Cannot receive directory to existing file".into());
        }
        Ok(dst_path.to_path_buf())
    } else {
        // dst doesn't exist
        if let Some(parent) = dst_path.parent() {
            if parent.is_dir() {
                // Parent is directory, treat dst as filename
                Ok(dst_path.to_path_buf())
            } else {
                // Need to create parent directories
                Ok(dst_path.to_path_buf())
            }
        } else {
            Ok(dst_path.to_path_buf())
        }
    }
}

fn prompt_overwrite(path: &Path) -> Result<bool> {
    print!("File {} already exists. Overwrite? (y/N): ", path.display());
    stdout().flush()?;
    
    let mut input = String::new();
    stdin().read_line(&mut input)?;
    
    let input = input.trim().to_lowercase();
    Ok(input == "y" || input == "yes")
}

fn get_available_space(path: &Path) -> Result<u64> {
    crate::diskspace::get_available_space(path)
}
