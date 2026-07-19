# Proc IOCTL Control Panel GUI

Run `launch_gui.cmd`, or open `index.html` directly in a browser.

This GUI is a local diagnostic dashboard for the safe test-data controller
contract. It does not load the kernel driver, open a COM port, or issue IOCTLs.

The dashboard mirrors the active safe paths:

- validation contract state,
- deterministic test payload preview,
- ring-buffer and overlapped-writer simulation,
- motion filter and fixed 8-byte packet preview,
- explicit unsafe-readmem disabled status.

`launch_gui.cmd` starts a local server on `127.0.0.1`, opens the dashboard, and
stops the server when the console window is closed or Enter is pressed.
