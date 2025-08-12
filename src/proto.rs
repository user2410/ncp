// Include the generated protobuf code
include!("generated/ncp.v1.rs");

use prost_types;

impl Probe {
    pub fn new(session_id: String, version: String, client_name: String) -> Self {
        Probe {
            session_id,
            version,
            capabilities: vec![],
            keepalive_seconds: 30,
            client_name,
        }
    }
}

impl Established {
    pub fn new(session_id: String, version: String) -> Self {
        Established {
            session_id,
            version,
            capabilities: vec![],
            server_time: Some(prost_types::Timestamp::from(std::time::SystemTime::now())),
        }
    }
}

impl Meta {
    pub fn new(session_id: String, file: FileMeta) -> Self {
        Meta { session_id, file: Some(file) }
    }
}

impl FileMeta {
    pub fn new(name: String, size: u64, is_dir: bool) -> Self {
        FileMeta {
            name,
            size,
            is_dir,
            mode: 0o644,
            mtime: Some(prost_types::Timestamp::from(std::time::SystemTime::now())),
            checksum_alg: "defaulthash".to_string(),
            checksum: vec![],
            attrs: std::collections::HashMap::new(),
        }
    }
}

impl PreflightOk {
    pub fn new(session_id: String, destination_exists: bool, available_space: u64) -> Self {
        PreflightOk {
            session_id,
            destination_exists,
            available_space,
            temp_path: String::new(),
        }
    }
}

impl PreflightFail {
    pub fn new(session_id: String, code: ErrorCode, reason: String) -> Self {
        PreflightFail {
            session_id,
            code: code as i32,
            reason,
        }
    }
}

impl TransferStart {
    pub fn new(session_id: String, file_size: u64) -> Self {
        TransferStart {
            session_id,
            mode: TransferMode::TransferRaw as i32,
            file_size,
            chunk_size: 0,
        }
    }
}

impl TransferResult {
    pub fn new(session_id: String, ok: bool, received_bytes: u64) -> Self {
        TransferResult {
            session_id,
            ok,
            code: if ok { ErrorCode::ErrorUnknown as i32 } else { ErrorCode::ErrChecksum as i32 },
            reason: String::new(),
            checksum: vec![],
            received_bytes,
        }
    }
}

impl Error {
    #[allow(dead_code)]
    pub fn new(session_id: String, code: ErrorCode, message: String) -> Self {
        Error {
            session_id,
            code: code as i32,
            message,
        }
    }
}

// Convert std errors to our error codes
impl From<std::io::Error> for ErrorCode {
    fn from(err: std::io::Error) -> Self {
        match err.kind() {
            std::io::ErrorKind::PermissionDenied => ErrorCode::ErrPermission,
            std::io::ErrorKind::UnexpectedEof => ErrorCode::ErrUnexpectedEof,
            std::io::ErrorKind::TimedOut => ErrorCode::ErrTimeout,
            _ => ErrorCode::ErrorUnknown,
        }
    }
}
