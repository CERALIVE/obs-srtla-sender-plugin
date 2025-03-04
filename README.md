# OBS SRTLA Sender Plugin (Linux Only)

This OBS Studio plugin adds support for using the SRTLA protocol to stream with connection bonding. It manages an SRTLA sender connection within OBS and provides bidirectional synchronization between OBS stream settings and the SRTLA sender configuration.

**Note: This plugin is designed for Linux only and is not compatible with Windows or macOS.**

## Features

- **Integrated SRTLA Sender**: Run SRTLA sender directly from OBS without needing external scripts
- **Bidirectional Settings Sync**: Automatic synchronization between OBS stream settings and SRTLA settings
- **Network Monitoring**: Automatically detects all active network interfaces (Ethernet, WiFi, cellular)
- **Connection Bonding**: Uses SRTLA to bond multiple connections for better streaming reliability
- **Dynamic Port Management**: Supports both fixed and random local ports
- **Automatic Connection Management**: Option to auto-start/stop the SRTLA sender with streaming
- **Configurable Latency**: Set custom SRT latency for different network conditions
- **Stream ID Support**: Configure custom stream IDs for authentication

## Requirements

- OBS Studio 28.0.0 or newer
- Linux system (tested on Ubuntu 20.04 and newer)
- BELABOX's SRT library compiled from [belabox/srt](https://github.com/BELABOX/srt) (not the standard Haivision SRT)
- SRTLA tools installed (`srtla_send` binary from [BELABOX SRTLA](https://github.com/BELABOX/srtla))
- Qt6 libraries (included with most modern Linux distributions)

## Installation

### Building from Source on Ubuntu

1. Install build dependencies:
   ```bash
   sudo apt-get update
   sudo apt-get install build-essential cmake qt6-base-dev libobs-dev pkg-config tclsh pkg-config openssl libssl-dev
   ```

2. Build BELABOX's SRT library:
   ```bash
   git clone https://github.com/BELABOX/srt.git
   cd srt
   ./configure
   make
   sudo make install
   sudo ldconfig
   cd ..
   ```

3. Clone and build the SRTLA binary:
   ```bash
   git clone https://github.com/BELABOX/srtla.git
   cd srtla
   make
   sudo cp srtla_send /usr/bin/
   cd ..
   ```

4. Clone and build this plugin:
   ```bash
   git clone https://github.com/CERALIVE/obs-srtla-sender-plugin.git
   cd obs-srtla-sender
   mkdir build && cd build
   # Build in Release mode for production use
   cmake -DCMAKE_BUILD_TYPE=Release ..
   make -j4
   # Install using
   sudo make install
   ```

5. Install the plugin (choose one method):
   ```bash
   # For system-wide OBS installation (requires sudo)
   sudo make install
   
   # For local user installation (without sudo)
   make install-user
   
   # For OBS installed via Flatpak
   make install-flatpak
   ```

## Usage

### Important: Plugin Loading

This plugin requires the OBS debug flag to load properly. You must start OBS with:

```bash
# Run OBS with plugin debugging enabled
OBS_DEBUG_PLUGINS=1 obs
```

Without this flag, the plugin may not appear in OBS. You can create a launcher or script to start OBS with this flag automatically.

### Normal Usage

1. **Open OBS Studio** and go to **Tools → SRTLA Sender → Settings**

2. Configure the SRTLA settings:
   - **SRTLA Server**: Address of the SRTLA relay server
   - **SRTLA Port**: Port of the SRTLA relay server (typically 5000-8000)
   - **Stream ID**: Optional identifier for your stream
   - **SRT Latency**: Buffer latency in milliseconds (2000ms recommended)
   - **Local Port**: Port for the local SRT connection (9000 default)
   - **Use Fixed Local Port**: Enable to use a consistent port
   - **Bidirectional Sync**: Enable to sync SRTLA settings with OBS stream settings

3. Configure your stream in OBS:
   - Go to **Settings → Stream**
   - Select **Custom** as the service
   - When bidirectional sync is enabled, the Server URL will be automatically populated with your SRTLA settings

4. Start/Stop the SRTLA sender:
   - Use **Tools → SRTLA Sender → Start/Stop SRTLA Sender** 
   - Or enable "Auto-start SRTLA when streaming starts" to manage it automatically

## How It Works

When the plugin is active with bidirectional sync enabled:

1. Changes to SRTLA settings in the plugin dialog are immediately reflected in OBS stream settings
2. Changes to OBS stream URL are detected and synchronized to the SRTLA settings
3. The plugin formats the URL as `srt://localhost:PORT?streamid=ID&latency=VALUE`
4. When streaming starts, the plugin launches the SRTLA sender process with the configured settings
5. The plugin monitors all network interfaces and automatically updates when connections change

## Troubleshooting

- **Connection Issues**: Ensure your firewall allows the required ports
- **Missing SRTLA Binary**: Verify that `srtla_send` is installed in /usr/bin
- **Plugin Not Loading**: Check OBS logs for any error messages
- **URL Not Updating**: Make sure bidirectional sync is enabled

## License

This plugin is released under the GNU General Public License v3.0 (GPL-3.0).

## Acknowledgments

- [BELABOX](https://github.com/BELABOX) for the SRTLA protocol
- SRT Alliance for the [SRT protocol](https://github.com/Haivision/srt)