use std::env;
use std::fs;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Tell cargo to recompile if Cargo.toml changes
    println!("cargo:rerun-if-changed=Cargo.toml");
    
    // Export the package version from Cargo.toml
    println!("cargo:rustc-env=CARGO_PKG_VERSION={}", env!("CARGO_PKG_VERSION"));
    
    // Ensure output directory exists
    let out_dir = "src/generated";
    fs::create_dir_all(out_dir)?;
    
    // Configure prost-build
    let mut config = prost_build::Config::new();
    config.out_dir(out_dir);
    
    // Compile the proto file
    config.compile_protos(&["proto/ncp.v1.proto"], &["proto/"])?;
    
    println!("Protobuf files compiled successfully to {}", out_dir);
    println!("Generated files:");
    
    // List generated files
    for entry in fs::read_dir(out_dir)? {
        let entry = entry?;
        println!("  - {}", entry.file_name().to_string_lossy());
    }
    
    Ok(())
}
