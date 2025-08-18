use std::process::Command;

pub fn get_hostname() -> String {
  #[cfg(any(unix, windows))]
  {
    let output = Command::new("hostname")
      .output()
      .expect("Failed to execute hostname command");

    String::from(String::from_utf8_lossy(&output.stdout).trim())
  }
  #[cfg(not(any(unix, windows)))]
  {
    String::from("ncp-client")
  }
}
