use std::collections::hash_map::DefaultHasher;
use std::hash::Hasher;
use std::io::Read;
use std::fs::File;
use std::path::Path;

use crate::types::Result;

/// Calculate checksum of a file using DefaultHasher
pub fn calculate_file_checksum<P: AsRef<Path>>(path: P) -> Result<Vec<u8>> {
    let mut file = File::open(path)?;
    let mut hasher = DefaultHasher::new();
    let mut buffer = [0u8; 8192];
    
    loop {
        let n = file.read(&mut buffer)?;
        if n == 0 {
            break;
        }
        hasher.write(&buffer[..n]);
    }
    
    let hash = hasher.finish();
    Ok(hash.to_be_bytes().to_vec())
}

/// Calculate checksum of bytes using DefaultHasher
#[allow(dead_code)]
pub fn calculate_bytes_checksum(data: &[u8]) -> Vec<u8> {
    let mut hasher = DefaultHasher::new();
    hasher.write(data);
    let hash = hasher.finish();
    hash.to_be_bytes().to_vec()
}

/// Streaming checksum calculator
pub struct StreamingChecksum {
    hasher: DefaultHasher,
}

impl StreamingChecksum {
    pub fn new() -> Self {
        Self {
            hasher: DefaultHasher::new(),
        }
    }
    
    pub fn update(&mut self, data: &[u8]) {
        self.hasher.write(data);
    }
    
    pub fn finalize(self) -> Vec<u8> {
        let hash = self.hasher.finish();
        hash.to_be_bytes().to_vec()
    }
}

impl Default for StreamingChecksum {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs::File;
    use std::io::Write;
    
    #[test]
    fn test_file_checksum() {
        // Create a temporary file manually
        let temp_path = "test_checksum_temp.txt";
        {
            let mut temp_file = File::create(temp_path).unwrap();
            temp_file.write_all(b"hello world").unwrap();
        }
        
        let checksum = calculate_file_checksum(temp_path).unwrap();
        assert!(!checksum.is_empty());
        assert_eq!(checksum.len(), 8); // u64 = 8 bytes
        
        // Clean up
        std::fs::remove_file(temp_path).unwrap();
    }
    
    #[test]
    fn test_bytes_checksum() {
        let checksum = calculate_bytes_checksum(b"hello world");
        assert!(!checksum.is_empty());
        assert_eq!(checksum.len(), 8);
    }
    
    #[test]
    fn test_streaming_checksum() {
        let mut stream = StreamingChecksum::new();
        stream.update(b"hello ");
        stream.update(b"world");
        let checksum = stream.finalize();
        
        let direct_checksum = calculate_bytes_checksum(b"hello world");
        assert_eq!(checksum, direct_checksum);
    }
}
