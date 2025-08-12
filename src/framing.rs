use prost::Message;
use std::io::{self, Read, Write};

use crate::types::Result;

/// Write a protobuf message with 4-byte big-endian length prefix
pub fn write_message<T: Message, W: Write>(writer: &mut W, message: &T) -> Result<()> {
    let mut buf = Vec::new();
    message.encode(&mut buf)?;
    
    let len = buf.len() as u32;
    writer.write_all(&len.to_be_bytes())?;
    writer.write_all(&buf)?;
    writer.flush()?;
    
    Ok(())
}

/// Read a protobuf message with 4-byte big-endian length prefix
pub fn read_message<T: Message + Default, R: Read>(reader: &mut R) -> Result<T> {
    // Read 4-byte length prefix
    let mut len_buf = [0u8; 4];
    reader.read_exact(&mut len_buf)?;
    let len = u32::from_be_bytes(len_buf);
    
    if len > 1024 * 1024 {  // 1MB max message size
        return Err("Message too large".into());
    }
    
    // Read message data
    let mut msg_buf = vec![0u8; len as usize];
    reader.read_exact(&mut msg_buf)?;
    
    // Decode protobuf message
    let message = T::decode(&msg_buf[..])?;
    Ok(message)
}

/// Helper to read raw bytes without length prefix (for file data)
pub fn read_exact_bytes<R: Read>(reader: &mut R, buf: &mut [u8]) -> io::Result<()> {
    reader.read_exact(buf)
}

/// Helper to write raw bytes without length prefix (for file data)
pub fn write_exact_bytes<W: Write>(writer: &mut W, buf: &[u8]) -> io::Result<()> {
    writer.write_all(buf)?;
    writer.flush()
}

#[cfg(test)]
mod tests {
    use crate::proto::Probe;

    use super::*;
    
    #[test]
    fn test_message_framing() {
        let probe = Probe::new(
            "test-session".to_string(),
            "0.1.0".to_string(),
            "test-client".to_string(),
        );
        
        let mut buf = Vec::new();
        write_message(&mut buf, &probe).unwrap();
        
        let mut cursor = std::io::Cursor::new(buf);
        let decoded: Probe = read_message(&mut cursor).unwrap();
        
        assert_eq!(decoded.session_id, "test-session");
        assert_eq!(decoded.version, "0.1.0");
        assert_eq!(decoded.client_name, "test-client");
    }
}
