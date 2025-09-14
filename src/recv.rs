use crate::protocol::*;
use crate::diskspace::{check_disk_space, format_bytes};
use crate::types::Result;
use crate::{OverwriteMode, vlog};

use std::fs::{self, File};
use std::io::{Write, BufWriter, stdin, stdout};
use std::net::{TcpListener, TcpStream};
use std::path::{Path, PathBuf};

pub fn execute(
    host: String,
    port: u16,
    dst: PathBuf,
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
        
        match handle_connection(stream, &dst, overwrite_mode.clone()) {
            Ok(()) => {
                println!("Transfer completed successfully");
                break;
            }
            Err(e) => {
                eprintln!("Transfer failed");
                vlog!(2, "{}", e);
                return Err(e);
            }
        }
    }

    Ok(())
}

fn handle_connection(
    mut stream: TcpStream,
    dst_path: &Path,
    overwrite_mode: OverwriteMode,
) -> Result<()> {
    vlog!(2, "Connection established");

    loop {
        let msg_type = match read_message_type(&mut stream) {
            Ok(t) => t,
            Err(_) => break,
        };
        
        if msg_type != MSG_META {
            return Err("Expected Meta message".into());
        }
        
        let _len = read_message_length(&mut stream)?;
        let file_meta = read_meta(&mut stream)?;
        
        let final_path = determine_final_path(dst_path, &file_meta.name, file_meta.is_dir)?;
        
        vlog!(2, "Receiving {}: {} ({} bytes) to {}", 
               if file_meta.is_dir { "directory" } else { "file" },
               file_meta.name, 
               file_meta.size,
               final_path.display());
        
        if file_meta.is_dir {
            handle_directory_entry(&mut stream, &final_path, &overwrite_mode)?;
        } else {
            handle_file_entry(&mut stream, &final_path, &file_meta, &overwrite_mode)?;
        }
    }
    
    Ok(())
}

fn handle_directory_entry(
    stream: &mut TcpStream,
    final_path: &Path,
    overwrite_mode: &OverwriteMode,
) -> Result<()> {
    if !final_path.exists() {
        fs::create_dir_all(final_path)?;
        vlog!(2, "Created directory: {:?}", final_path);
    } else {
        match overwrite_mode {
            OverwriteMode::Ask => {
                if !prompt_overwrite(final_path)? {
                    let preflight_fail = PreflightFail {
                        reason: "User declined directory overwrite".to_string(),
                    };
                    write_preflight_fail(stream, &preflight_fail)?;
                    return Ok(());
                }
            }
            OverwriteMode::No => {
                vlog!(2, "Directory exists, continuing: {:?}", final_path);
            }
            OverwriteMode::Yes => {}
        }
    }
    
    let preflight_ok = PreflightOk { available_space: 0 };
    write_preflight_ok(stream, &preflight_ok)?;
    
    Ok(())
}

fn handle_file_entry(
    stream: &mut TcpStream,
    final_path: &Path,
    file_meta: &FileMeta,
    overwrite_mode: &OverwriteMode,
) -> Result<()> {
    if final_path.exists() {
        match overwrite_mode {
            OverwriteMode::Ask => {
                if !prompt_overwrite(final_path)? {
                    let preflight_fail = PreflightFail {
                        reason: "User declined overwrite".to_string(),
                    };
                    write_preflight_fail(stream, &preflight_fail)?;
                    return Ok(());
                }
            }
            OverwriteMode::No => {
                vlog!(2, "File exists, skipping: {}", final_path.display());
                let preflight_fail = PreflightFail {
                    reason: "File exists, skipping".to_string(),
                };
                write_preflight_fail(stream, &preflight_fail)?;
                return Ok(());
            }
            OverwriteMode::Yes => {}
        }
    }

    if let Some(parent) = final_path.parent() {
        fs::create_dir_all(parent)?;
    }

    let available_space = get_available_space(final_path)?;
    vlog!(2, "Available disk space: {} bytes", available_space);

    let has_enough_space = check_disk_space(final_path, file_meta.size)?;
    
    if !has_enough_space {
        let error_msg = format!(
            "Insufficient disk space. Need: {}, Available: {}",
            format_bytes(file_meta.size),
            format_bytes(available_space)
        );
        vlog!(2, "{}", error_msg);
        
        let preflight_fail = PreflightFail { reason: error_msg };
        write_preflight_fail(stream, &preflight_fail)?;
        return Err("Insufficient disk space".into());
    }

    let preflight_ok = PreflightOk { available_space };
    write_preflight_ok(stream, &preflight_ok)?;

    let msg_type = read_message_type(stream)?;
    let _len = read_message_length(stream)?;
    
    if msg_type != MSG_TRANSFER_START {
        return Err("Expected TransferStart message".into());
    }
    
    let transfer_start = read_transfer_start(stream)?;
    
    let temp_path = final_path.with_extension("ncp_temp");
    let temp_file = File::create(&temp_path)?;
    let mut writer = BufWriter::new(temp_file);
    
    let mut total_bytes = 0u64;
    let mut buffer = [0u8; 8192];
    let file_size = transfer_start.file_size;
    
    while total_bytes < file_size {
        let remaining = (file_size - total_bytes) as usize;
        let to_read = remaining.min(buffer.len());
        
        read_exact_bytes(stream, &mut buffer[..to_read])?;
        writer.write_all(&buffer[..to_read])?;
        
        total_bytes += to_read as u64;
        
        if total_bytes % (1024 * 1024) == 0 || total_bytes == file_size {
            print!("\rReceived: {}/{} bytes", total_bytes, file_size);
            stdout().flush().unwrap();
        }
    }
    println!();
    
    writer.flush()?;
    drop(writer);

    fs::rename(&temp_path, final_path)?;
    vlog!(2, "File saved to: {}", final_path.display());
    
    let transfer_result = TransferResult { ok: true, received_bytes: total_bytes };
    write_transfer_result(stream, &transfer_result)?;

    Ok(())
}

fn determine_final_path(dst_path: &Path, file_name: &str, is_dir: bool) -> Result<PathBuf> {
    if dst_path.is_dir() {
        Ok(dst_path.join(file_name))
    } else if dst_path.exists() {
        if is_dir {
            return Err("Cannot receive directory to existing file".into());
        }
        Ok(dst_path.to_path_buf())
    } else {
        Ok(dst_path.to_path_buf())
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