use std::fs;
use std::path::{Path, PathBuf};
use crate::types::Result;
use crate::vlog;

#[derive(Debug, Clone)]
pub struct FileEntry {
    pub path: PathBuf,
    pub relative_path: PathBuf,
    pub is_dir: bool,
    pub size: u64,
}

/// Recursively walk directory and collect all files and directories
pub fn walk_directory<P: AsRef<Path>>(root: P) -> Result<Vec<FileEntry>> {
    let root = root.as_ref();
    let mut entries = Vec::new();
    
    vlog!(2, "Walking directory: {:?}", root);
    walk_recursive(root, root, &mut entries)?;
    
    // Sort entries: directories first, then files, both alphabetically
    entries.sort_by(|a, b| {
        match (a.is_dir, b.is_dir) {
            (true, false) => std::cmp::Ordering::Less,
            (false, true) => std::cmp::Ordering::Greater,
            _ => a.relative_path.cmp(&b.relative_path),
        }
    });
    
    vlog!(1, "Found {} entries in directory", entries.len());
    Ok(entries)
}

fn walk_recursive(root: &Path, current: &Path, entries: &mut Vec<FileEntry>) -> Result<()> {
    let metadata = fs::metadata(current)?;
    let relative_path = current.strip_prefix(root)
        .map_err(|_| "Failed to create relative path")?
        .to_path_buf();
    
    if metadata.is_dir() {
        // Add directory entry
        entries.push(FileEntry {
            path: current.to_path_buf(),
            relative_path: relative_path.clone(),
            is_dir: true,
            size: 0,
        });
        
        vlog!(2, "Directory: {:?}", relative_path);
        
        // Recursively process directory contents
        let dir_entries = fs::read_dir(current)?;
        for entry in dir_entries {
            let entry = entry?;
            walk_recursive(root, &entry.path(), entries)?;
        }
    } else {
        // Add file entry
        entries.push(FileEntry {
            path: current.to_path_buf(),
            relative_path,
            is_dir: false,
            size: metadata.len(),
        });
        
        vlog!(2, "File: {:?} ({} bytes)", current, metadata.len());
    }
    
    Ok(())
}

/// Calculate total size of all files in entries
pub fn calculate_total_size(entries: &[FileEntry]) -> u64 {
    entries.iter()
        .filter(|e| !e.is_dir)
        .map(|e| e.size)
        .sum()
}