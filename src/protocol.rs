use std::io::{Read, Write};
use crate::types::Result;

// Simple binary protocol without protobuf
// Message format: [type:u8][len:u32][data]

pub const MSG_META: u8 = 1;
pub const MSG_PREFLIGHT_OK: u8 = 2;
pub const MSG_PREFLIGHT_FAIL: u8 = 3;
pub const MSG_TRANSFER_START: u8 = 4;
pub const MSG_TRANSFER_RESULT: u8 = 5;

#[derive(Debug)]
pub struct FileMeta {
    pub name: String,
    pub size: u64,
    pub is_dir: bool,
}

#[derive(Debug)]
pub struct PreflightOk {
    pub available_space: u64,
}

#[derive(Debug)]
pub struct PreflightFail {
    pub reason: String,
}

#[derive(Debug)]
pub struct TransferStart {
    pub file_size: u64,
}

#[derive(Debug)]
pub struct TransferResult {
    pub ok: bool,
    pub received_bytes: u64,
}

pub fn write_meta<W: Write>(writer: &mut W, meta: &FileMeta) -> Result<()> {
    let name_bytes = meta.name.as_bytes();
    let len = 8 + 1 + 4 + name_bytes.len();
    
    writer.write_all(&[MSG_META])?;
    writer.write_all(&(len as u32).to_be_bytes())?;
    writer.write_all(&meta.size.to_be_bytes())?;
    writer.write_all(&[if meta.is_dir { 1 } else { 0 }])?;
    writer.write_all(&(name_bytes.len() as u32).to_be_bytes())?;
    writer.write_all(name_bytes)?;

    writer.flush()?;
    Ok(())
}

pub fn read_meta<R: Read>(reader: &mut R) -> Result<FileMeta> {
    // Message header (type + length) already read by caller
    let mut buf = [0u8; 8];
    reader.read_exact(&mut buf)?;
    let size = u64::from_be_bytes(buf);
    
    let mut buf = [0u8; 1];
    reader.read_exact(&mut buf)?;
    let is_dir = buf[0] != 0;
    
    let mut buf = [0u8; 4];
    reader.read_exact(&mut buf)?;
    let name_len = u32::from_be_bytes(buf) as usize;
    
    let mut name_buf = vec![0u8; name_len];
    reader.read_exact(&mut name_buf)?;
    let name = String::from_utf8(name_buf).map_err(|_| "Invalid UTF-8")?;
    
    Ok(FileMeta { name, size, is_dir })
}

pub fn write_preflight_ok<W: Write>(writer: &mut W, msg: &PreflightOk) -> Result<()> {
    writer.write_all(&[MSG_PREFLIGHT_OK])?;
    writer.write_all(&8u32.to_be_bytes())?;
    writer.write_all(&msg.available_space.to_be_bytes())?;
    writer.flush()?;
    Ok(())
}

pub fn read_preflight_ok<R: Read>(reader: &mut R) -> Result<PreflightOk> {
    let mut buf = [0u8; 8];
    reader.read_exact(&mut buf)?;
    let available_space = u64::from_be_bytes(buf);
    Ok(PreflightOk { available_space })
}

pub fn write_preflight_fail<W: Write>(writer: &mut W, msg: &PreflightFail) -> Result<()> {
    let reason_bytes = msg.reason.as_bytes();
    let len = 4 + reason_bytes.len();
    
    writer.write_all(&[MSG_PREFLIGHT_FAIL])?;
    writer.write_all(&(len as u32).to_be_bytes())?;
    writer.write_all(&(reason_bytes.len() as u32).to_be_bytes())?;
    writer.write_all(reason_bytes)?;
    writer.flush()?;
    Ok(())
}

pub fn read_preflight_fail<R: Read>(reader: &mut R) -> Result<PreflightFail> {
    let mut buf = [0u8; 4];
    reader.read_exact(&mut buf)?;
    let reason_len = u32::from_be_bytes(buf) as usize;
    
    let mut reason_buf = vec![0u8; reason_len];
    reader.read_exact(&mut reason_buf)?;
    let reason = String::from_utf8(reason_buf).map_err(|_| "Invalid UTF-8")?;
    
    Ok(PreflightFail { reason })
}

pub fn write_transfer_start<W: Write>(writer: &mut W, msg: &TransferStart) -> Result<()> {
    writer.write_all(&[MSG_TRANSFER_START])?;
    writer.write_all(&8u32.to_be_bytes())?;
    writer.write_all(&msg.file_size.to_be_bytes())?;
    writer.flush()?;
    Ok(())
}

pub fn read_transfer_start<R: Read>(reader: &mut R) -> Result<TransferStart> {
    let mut buf = [0u8; 8];
    reader.read_exact(&mut buf)?;
    let file_size = u64::from_be_bytes(buf);
    Ok(TransferStart { file_size })
}

pub fn write_transfer_result<W: Write>(writer: &mut W, msg: &TransferResult) -> Result<()> {
    writer.write_all(&[MSG_TRANSFER_RESULT])?;
    writer.write_all(&9u32.to_be_bytes())?;
    writer.write_all(&[if msg.ok { 1 } else { 0 }])?;
    writer.write_all(&msg.received_bytes.to_be_bytes())?;
    writer.flush()?;
    Ok(())
}

pub fn read_transfer_result<R: Read>(reader: &mut R) -> Result<TransferResult> {
    let mut buf = [0u8; 1];
    reader.read_exact(&mut buf)?;
    let ok = buf[0] != 0;
    
    let mut buf = [0u8; 8];
    reader.read_exact(&mut buf)?;
    let received_bytes = u64::from_be_bytes(buf);
    
    Ok(TransferResult { ok, received_bytes })
}

pub fn read_message_type<R: Read>(reader: &mut R) -> Result<u8> {
    let mut buf = [0u8; 1];
    reader.read_exact(&mut buf)?;
    Ok(buf[0])
}

pub fn read_message_length<R: Read>(reader: &mut R) -> Result<u32> {
    let mut buf = [0u8; 4];
    reader.read_exact(&mut buf)?;
    Ok(u32::from_be_bytes(buf))
}

pub fn write_raw_bytes<W: Write>(writer: &mut W, data: &[u8]) -> Result<()> {
    writer.write_all(data)?;
    writer.flush()?;
    Ok(())
}

pub fn read_exact_bytes<R: Read>(reader: &mut R, buf: &mut [u8]) -> Result<()> {
    reader.read_exact(buf)?;
    Ok(())
}