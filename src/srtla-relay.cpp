/**
 * SRTLA Relay Plugin for OBS Studio
 * Implementation of SrtlaRelay class
 * 
 * Provides functionality to connect to an SRTLA relay server
 * from OBS Studio.
 *
 * Author: Andres Cera
 * License: GPL-3.0
 */

#include "srtla-relay.h"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <random>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <netdb.h>
#include <arpa/inet.h>
#include <cctype>
#include <algorithm>

// Include Qt headers
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMessageBox>
#include <QtCore/QCoreApplication>
#include <QtCore/QString>
#include <QtCore/QMetaObject>

#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#define PROCESS_KILL(pid) kill(pid, SIGTERM)

namespace fs = std::filesystem;

// Forward declarations for callbacks
static bool srtla_service_selected(obs_properties_t *props, obs_property_t *property, obs_data_t *settings);
static bool apply_srtla_settings(obs_properties_t *props, obs_property_t *property, void *data);

SrtlaRelay::SrtlaRelay()
    : m_port(3000), 
      m_localPort(9000),  // Default to port 9000
      m_processRunning(false),
      m_processId(-1),
      m_autoStart(false),
      m_latency(2000),
      m_bidirectionalSync(true), // Default bidirectional sync on
      m_useFixedPort(true) {  // Default to using fixed port
          
    // Create directory for IP list file if it doesn't exist
    std::string tempPath;
    
    // Linux: always use ~/srtla_relay_temp as requested
    tempPath = "~/srtla_relay_temp";
    
    // Expand the ~ to the actual home directory for internal operations
    const char* home = getenv("HOME");
    if (home) {
        std::string expandedPath = std::string(home) + "/srtla_relay_temp";
        
        // Only use this path internally for file operations, keep the ~ version for display
        if (!fs::exists(expandedPath)) {
            try {
                fs::create_directories(expandedPath);
            } catch (const fs::filesystem_error&) {
                tempPath = "/tmp/srtla_relay_temp";
            }
        }
    } else {
        tempPath = "/tmp/srtla_relay_temp";
    }
    
    // Ensure directory exists
    if (!fs::exists(tempPath)) {
        try {
            fs::create_directories(tempPath);
        } catch (const fs::filesystem_error&) {
            // Fall back to system temp if we can't create in user directory
            tempPath = "/tmp/srtla_relay_temp";
            fs::create_directory(tempPath);
        }
    }
    
    m_ipListPath = tempPath + "/ip_bank.txt";

    blog(LOG_INFO, "Using IP bank file: %s", m_ipListPath.c_str());
    
    // Create network monitor
    m_networkMonitor = std::make_unique<NetworkMonitor>();
    
    // Register callback for network changes
    m_networkMonitor->registerCallback([this](const std::vector<NetworkInterface>& interfaces) {
        this->onNetworkChange(interfaces);
    });
}

SrtlaRelay::~SrtlaRelay() {
    stopSrtlaProcess();
    
    // Clean up temp files
    if (fs::exists(m_ipListPath)) {
        fs::remove(m_ipListPath);
    }
    
    std::string tempPath = fs::path(m_ipListPath).parent_path().string();
    if (fs::exists(tempPath)) {
        fs::remove(tempPath);
    }
}

void SrtlaRelay::init() {
    // Start network monitoring
    m_networkMonitor->start();
    
    // Load settings
    loadSettings();
    
    // Service type is registered in obs_module_load in plugin-main.cpp
    
    // Set up properties
    setupProperties();
    
    // We no longer need to hook into the service change signal as we now have our own service
    // that gets initialized directly via the initialize callback
}

bool SrtlaRelay::saveSettings() {
    obs_data_t *settings = obs_data_create();
    
    obs_data_set_string(settings, "srtla_server", m_server.c_str());
    obs_data_set_int(settings, "srtla_port", m_port);
    obs_data_set_string(settings, "srtla_stream_id", m_streamId.c_str());
    obs_data_set_bool(settings, "srtla_auto_start", m_autoStart);
    obs_data_set_int(settings, "srtla_latency", m_latency);
    obs_data_set_bool(settings, "srtla_use_fixed_port", m_useFixedPort);
    obs_data_set_int(settings, "srtla_local_port", m_localPort);
    obs_data_set_bool(settings, "srtla_bidirectional_sync", m_bidirectionalSync);
    
    blog(LOG_INFO, "Settings values being saved: server=%s, port=%d, stream_id=%s, latency=%d, use_fixed_port=%d, local_port=%d, bidirectional_sync=%d", 
         m_server.c_str(), m_port, m_streamId.c_str(), m_latency, m_useFixedPort, m_localPort, m_bidirectionalSync);
    
    // Use a location in the user's home directory where we have write permissions
    const char* home = getenv("HOME");
    std::string configDir;
    
    if (home) {
        configDir = std::string(home) + "/.config/obs-studio";
    } else {
        configDir = "/tmp";
    }
    
    std::string configPath = configDir + "/srtla_settings.json";
    
    // Check if config file already exists
    bool fileExists = fs::exists(configPath);
    
    // Ensure config directory exists
    if (!fs::exists(configDir)) {
        try {
            fs::create_directories(configDir);
        } catch (const fs::filesystem_error&) {
            blog(LOG_ERROR, "Failed to create config directory: %s", configDir.c_str());
            obs_data_release(settings);
            return false;
        }
    }
    
    obs_data_save_json(settings, configPath.c_str());
    obs_data_release(settings);
    
    return fileExists;
}

void SrtlaRelay::loadSettings() {
    // Use a location in the user's home directory where we have write permissions
    const char* home = getenv("HOME");
    std::string configPath;
    
    if (home) {
        configPath = std::string(home) + "/.config/obs-studio/srtla_settings.json";
    } else {
        configPath = "/tmp/srtla_settings.json";
    }
    
    // Set defaults first
    m_server = "";
    m_port = 3000;
    m_streamId = "";
    m_autoStart = false;
    m_latency = 2000;  // Default latency: 2000ms
    m_useFixedPort = true;  // Default to using fixed port
    m_localPort = 9000;  // Default local port: 9000
    
    // Check if config file exists
    if (!fs::exists(configPath)) {
        return;
    }
    
    obs_data_t *settings = obs_data_create_from_json_file(configPath.c_str());
    if (settings) {
        const char* server = obs_data_get_string(settings, "srtla_server");
        m_server = server ? server : "";
        
        m_port = (uint16_t)obs_data_get_int(settings, "srtla_port");
        if (m_port == 0) m_port = 3000; // Default port
        
        const char* streamId = obs_data_get_string(settings, "srtla_stream_id");
        m_streamId = streamId ? streamId : "";
        
        // Load auto-start setting (default to off for safety)
        m_autoStart = obs_data_get_bool(settings, "srtla_auto_start");
        
        // Load latency setting (default to 2000ms)
        m_latency = (int)obs_data_get_int(settings, "srtla_latency");
        if (m_latency < 1000 || m_latency > 8000) m_latency = 2000; // Ensure valid range
        
        // Load fixed port settings
        m_useFixedPort = obs_data_get_bool(settings, "srtla_use_fixed_port");
        m_localPort = (uint16_t)obs_data_get_int(settings, "srtla_local_port");
        if (m_localPort == 0) m_localPort = 9000; // Default local port
        
        // Load bidirectional sync setting (default to on)
        m_bidirectionalSync = obs_data_get_bool(settings, "srtla_bidirectional_sync");
        if (!obs_data_has_user_value(settings, "srtla_bidirectional_sync")) {
            m_bidirectionalSync = true; // Default to enabled if not set
        }
        
        obs_data_release(settings);
    }
}

bool SrtlaRelay::restartWithPort(uint16_t port) {
    // If running, stop first
    if (m_processRunning) {
        stopSrtlaProcess();
    }
    
    // Set the port
    m_localPort = port;
    blog(LOG_INFO, "Restarting SRTLA process with port: %d", m_localPort);
    
    // Start the process with the specified port
    return startSrtlaProcess();
}

bool SrtlaRelay::startSrtlaProcess() {
    if (m_server.empty()) {
        blog(LOG_ERROR, "SRTLA server not configured");
        return false;
    }
    
    // If bidirectional sync is enabled, always use fixed port
    if (m_bidirectionalSync) {
        // Force fixed port when bidirectional sync is enabled
        if (m_localPort == 0) {
            m_localPort = 9000; // Use default if not set
        }
        // Make sure fixed port is enabled
        setUseFixedPort(true);
        blog(LOG_INFO, "Bidirectional sync enabled, using fixed local port: %d", m_localPort);
    }
    // Otherwise use fixed or random port based on settings
    else if (!m_useFixedPort || m_localPort == 0) {
        m_localPort = generateRandomPort();
        blog(LOG_INFO, "Using random local port: %d", m_localPort);
    } else {
        blog(LOG_INFO, "Using fixed local port: %d", m_localPort);
    }
    
    // Get the real path (with ~ expanded) for the IP bank file
    const char* home = getenv("HOME");
    std::string realIpPath;
    if (home) {
        realIpPath = std::string(home) + "/srtla_relay_temp/ip_bank.txt";
    } else {
        realIpPath = "/tmp/srtla_relay_temp/ip_bank.txt";
    }
    
    // Ensure the directory exists
    std::string dirPath = fs::path(realIpPath).parent_path().string();
    if (!fs::exists(dirPath)) {
        try {
            fs::create_directories(dirPath);
            blog(LOG_INFO, "Created directory for IP bank: %s", dirPath.c_str());
        } catch (const fs::filesystem_error&) {
            blog(LOG_ERROR, "Failed to create directory for IP bank: %s", dirPath.c_str());
            return false;
        }
    }
    
    // Create the IP list file with dynamically detected network interfaces
    std::ofstream ipFile(realIpPath);
    if (!ipFile.is_open()) {
        blog(LOG_ERROR, "Failed to create IP list file: %s", realIpPath.c_str());
        return false;
    }
    
    // Get all network interfaces
    std::vector<NetworkInterface> interfaces = m_networkMonitor->detectNetworkInterfaces();
    
    bool added = false;
    std::string ipList;
    
    // Add all active, non-loopback interfaces
    for (const auto& iface : interfaces) {
        if (iface.isActive && !iface.ipAddress.empty() && 
            iface.ipAddress != "127.0.0.1" && iface.name != "lo") {
            
            ipFile << iface.ipAddress << std::endl;
            ipList += iface.ipAddress + " ";
            added = true;
        }
    }
    
    // If no interfaces found, add a default one to prevent errors
    if (!added) {
        // Most common local network IP
        ipFile << "192.168.1.100" << std::endl;
        ipList = "192.168.1.100 (fallback)";
    }
    
    ipFile.close();
    
    blog(LOG_INFO, "Created IP list file with dynamic IPs [%s] at: %s", 
         ipList.c_str(), realIpPath.c_str());
    
    // Build command
    std::string cmd;
    
    // Get DNS resolution for the server address
    std::string resolvedServer = m_server;
    if (!m_server.empty() && !isdigit(m_server[0])) {
        // Try to resolve the hostname
        blog(LOG_INFO, "Resolving hostname: %s", m_server.c_str());
        struct hostent *he = gethostbyname(m_server.c_str());
        if (he != nullptr) {
            char ip[INET_ADDRSTRLEN];
            struct in_addr **addr_list = (struct in_addr **)he->h_addr_list;
            
            if (addr_list[0] != nullptr) {
                inet_ntop(AF_INET, addr_list[0], ip, INET_ADDRSTRLEN);
                resolvedServer = ip;
                blog(LOG_INFO, "Resolved %s to IP: %s", m_server.c_str(), resolvedServer.c_str());
            }
        } else {
            blog(LOG_WARNING, "Could not resolve hostname, using as-is: %s", m_server.c_str());
        }
    }
    
    // Linux version - use the realIpPath we created earlier
    cmd = "/usr/bin/srtla_send " + 
          std::to_string(m_localPort) + " " + 
          resolvedServer + " " + 
          std::to_string(m_port) + " " +
          realIpPath + " >> /tmp/srtla.log 2>&1 &";
    
    blog(LOG_INFO, "Starting SRTLA process with command: %s", cmd.c_str());
    
    int result = system(cmd.c_str());
    if (result != 0) {
        blog(LOG_ERROR, "Failed to start SRTLA process (code: %d)", result);
        return false;
    }
    
    // Mark as running
    m_processRunning = true;
    
    // Try to find PID of the process
    // This could be improved with a more reliable way to get the PID
    std::string findPidCmd = "pgrep -f 'srtla_send " + std::to_string(m_localPort) + "'";
    FILE* pipe = popen(findPidCmd.c_str(), "r");
    if (pipe) {
        char buffer[128];
        if (fgets(buffer, sizeof(buffer), pipe) != NULL) {
            m_processId = atoi(buffer);
            blog(LOG_INFO, "SRTLA process started with PID: %d", m_processId);
        }
        pclose(pipe);
    }
    
    return true;
}

void SrtlaRelay::stopSrtlaProcess() {
    if (!m_processRunning) {
        blog(LOG_INFO, "SRTLA process is not running");
        return;
    }
    
    blog(LOG_INFO, "Stopping SRTLA process");
    
    // Try to kill the process by PID first
    if (m_processId > 0) {
        blog(LOG_INFO, "Killing process with PID: %d", m_processId);
        PROCESS_KILL(m_processId);
    } else {
        // Alternative: Kill all SRTLA processes
        killSrtlaProcess();
    }
    
    // Reset state
    m_processRunning = false;
    m_processId = -1;
}

void SrtlaRelay::killSrtlaProcess() {
    blog(LOG_INFO, "Attempting to stop SRTLA process");
    
    if (m_processId > 0) {
        blog(LOG_INFO, "Killing process with ID: %d", m_processId);
        PROCESS_KILL(m_processId);
    } else {
        // Alternative: Kill all SRTLA processes
        blog(LOG_INFO, "No PID available, killing all srtla_send processes");
        system("pkill -f srtla_send");
    }
}

void SrtlaRelay::onNetworkChange(const std::vector<NetworkInterface>& interfaces) {
    blog(LOG_INFO, "Network change detected - updating IP bank file");
    
    // Update IP list file
    if (m_networkMonitor->saveIpListToFile(m_ipListPath)) {
        blog(LOG_INFO, "IP bank file updated successfully");
    } else {
        blog(LOG_ERROR, "Failed to update IP bank file after network change");
    }
    
    // If process is running, send HUP signal to reload the IP list
    if (m_processRunning) {
        blog(LOG_INFO, "Sending HUP signal to SRTLA process to reload IP list");
        // Linux - use killall with HUP signal
        system("killall -HUP srtla_send");
        blog(LOG_INFO, "HUP signal sent to reload IP list");
    }
}

uint16_t SrtlaRelay::generateRandomPort() const {
    // Generate random port between 10000 and 65000
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint16_t> distrib(10000, 65000);
    
    return distrib(gen);
}

// Extract SRT parameters from URL
bool SrtlaRelay::extractSRTParamsFromURL(const std::string& url, uint16_t& port, int& latency, std::string& streamId) {
    // Save the original port value before extraction
    uint16_t originalPort = port;
    blog(LOG_INFO, "Port value before extraction: %d", originalPort);
    
    // Initialize for the extraction process
    latency = 2000;  // Default latency
    streamId = "";
    
    blog(LOG_INFO, "Parsing SRT URL: %s", url.c_str());
    
    // Handle various SRT URL formats:
    // srt://localhost:port
    // srt://localhost:port?latency=number
    // srt://localhost:port?latency=number&streamid=string
    // srt://localhost:port?streamid=string
    // srt://localhost:port?streamid=string&latency=number
    
    if (url.empty()) {
        blog(LOG_WARNING, "Empty SRT URL");
        // Keep original port value
        port = originalPort;
        blog(LOG_INFO, "Keeping original port value: %d", port);
        return false;
    }
    
    // Check if it's an SRT URL
    if (url.compare(0, 6, "srt://") != 0) {
        blog(LOG_WARNING, "Not an SRT URL: %s", url.c_str());
        // Keep original port value
        port = originalPort;
        blog(LOG_INFO, "Keeping original port value: %d", port);
        return false;
    }
    
    // Extract host:port part
    size_t hostStart = 6; // After "srt://"
    size_t hostEnd = url.find('?', hostStart);
    std::string hostPort = url.substr(hostStart, (hostEnd != std::string::npos) ? hostEnd - hostStart : std::string::npos);
    
    blog(LOG_INFO, "Host:Port part: %s", hostPort.c_str());
    
    // Extract port
    size_t portPos = hostPort.find(':');
    if (portPos != std::string::npos) {
        std::string portStr = hostPort.substr(portPos + 1);
        try {
            uint16_t extractedPort = static_cast<uint16_t>(std::stoi(portStr));
            
            // Only use the extracted port if it's valid
            if (extractedPort > 0) {
                port = extractedPort;
                blog(LOG_INFO, "Found valid port in URL: %d", port);
            } else {
                // Keep the original port if extracted port is 0 or invalid
                port = originalPort;
                blog(LOG_WARNING, "Found invalid port (0) in URL, keeping original: %d", port);
            }
        } catch (const std::exception& e) {
            blog(LOG_WARNING, "Failed to parse port in SRT URL: %s - %s", url.c_str(), e.what());
            // Keep the original port value instead of using a default
            port = originalPort;
            blog(LOG_INFO, "Keeping original port after parse failure: %d", port);
        }
    } else {
        // No port in URL, keep the original port
        blog(LOG_INFO, "No port specified in URL, keeping original: %d", originalPort);
        port = originalPort;
    }
    
    // Extract parameters if present
    if (hostEnd != std::string::npos) {
        std::string params = url.substr(hostEnd + 1);
        blog(LOG_INFO, "URL parameters: %s", params.c_str());
        
        // Parse parameters - split by &
        size_t paramStart = 0;
        size_t paramEnd;
        
        do {
            paramEnd = params.find('&', paramStart);
            std::string param = params.substr(paramStart, (paramEnd != std::string::npos) ? paramEnd - paramStart : std::string::npos);
            
            // Split by =
            size_t nameEnd = param.find('=');
            if (nameEnd != std::string::npos) {
                std::string name = param.substr(0, nameEnd);
                std::string value = param.substr(nameEnd + 1);
                
                blog(LOG_INFO, "Found parameter: %s = %s", name.c_str(), value.c_str());
                
                // Process known parameters (case-insensitive comparison manually)
                std::string nameLower = name;
                for (char& c : nameLower) {
                    c = std::tolower(c);
                }
                
                if (nameLower == "latency" || nameLower == "delay") {
                    try {
                        latency = std::stoi(value);
                        blog(LOG_INFO, "Parsed latency: %d", latency);
                    } catch (const std::exception& e) {
                        blog(LOG_WARNING, "Failed to parse latency value in SRT URL: %s", value.c_str());
                    }
                } else if (nameLower == "streamid") {
                    streamId = value;
                    blog(LOG_INFO, "Found streamid: %s", streamId.c_str());
                }
            }
            
            paramStart = (paramEnd != std::string::npos) ? paramEnd + 1 : std::string::npos;
        } while (paramStart != std::string::npos);
    }
    
    blog(LOG_INFO, "Extracted SRT parameters - port: %d, latency: %d, streamId: %s", 
         port, latency, streamId.c_str());
    
    return true;
}

// Build SRT URL from parameters
std::string SrtlaRelay::buildSRTURL(uint16_t port, int latency, const std::string& streamId) {
    std::string url;
    
    // Use localhost for better readability and consistency with OBS UI
    if (port > 0) {
        // Use the provided port value
        url = "srt://localhost:" + std::to_string(port);
        blog(LOG_INFO, "Using specified port for URL: %d", port);
    } else {
        // If port is 0 or invalid, use the current local port from instance
        url = "srt://localhost:" + std::to_string(m_localPort);
        blog(LOG_INFO, "Using current instance port for URL: %d", m_localPort);
    }
    
    // Include both streamId and latency parameters if specified
    bool hasParam = false;
    
    // Add streamId parameter first
    if (!streamId.empty()) {
        url += "?streamid=" + streamId;
        hasParam = true;
    }
    
    // Always add latency parameter
    // Even if outside the optimal range, include it to ensure persistence
    int usedLatency = (latency >= 1000) ? latency : m_latency;
    url += (hasParam ? "&" : "?") + std::string("latency=") + std::to_string(usedLatency);
    
    blog(LOG_INFO, "Built SRT URL: %s", url.c_str());
    return url;
}

// Get the current OBS stream server URL
// Implement the forceful update of OBS stream URL
bool SrtlaRelay::forceUpdateOBSStreamURL(const std::string& newUrl) {
    blog(LOG_INFO, "Force updating OBS Stream URL to: %s", newUrl.c_str());
    
    // Try multiple methods to ensure URL gets updated
    bool success = false;
    
    // Method 1: Update service settings directly
    obs_service_t* service = obs_frontend_get_streaming_service();
    if (service) {
        obs_data_t* settings = obs_data_create();
        obs_data_set_string(settings, "url", newUrl.c_str());
        obs_data_set_string(settings, "server", newUrl.c_str());
        obs_service_update(service, settings);
        obs_data_release(settings);
        
        // Force UI refresh by reapplying the service
        obs_frontend_set_streaming_service(service);
        
        blog(LOG_INFO, "Updated service URL through API");
        success = true;
    }
    
    // Method 2: Try to find the config files directly
    const char* home = getenv("HOME");
    if (home) {
        // Try common profile paths
        std::vector<std::string> possiblePaths = {
            std::string(home) + "/.config/obs-studio/basic/profiles/Untitled/service.json",
            std::string(home) + "/.config/obs-studio/basic/profiles/Default/service.json",
            std::string(home) + "/.config/obs-studio/basic/profiles/default/service.json",
            std::string(home) + "/.config/obs-studio/basic/service.json"
        };
        
        // Try each path
        for (const auto& path : possiblePaths) {
            blog(LOG_INFO, "Checking for OBS config at: %s", path.c_str());
            
            if (fs::exists(path)) {
                blog(LOG_INFO, "Found OBS service config at: %s - attempting direct edit", path.c_str());
                
                // Read current content
                std::ifstream inFile(path);
                std::string content((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
                inFile.close();
                
                blog(LOG_INFO, "Current config content (excerpt): %.100s...", content.c_str());
                
                // Use brute-force search and replace for the URL and server patterns
                // First try URL field
                bool updated = false;
                size_t urlPos = content.find("\"url\":");
                if (urlPos != std::string::npos) {
                    // Find the beginning and end of the URL value
                    size_t valueStart = content.find("\"", urlPos + 6) + 1;
                    size_t valueEnd = content.find("\"", valueStart);
                    
                    if (valueStart != std::string::npos && valueEnd != std::string::npos) {
                        std::string oldUrl = content.substr(valueStart, valueEnd - valueStart);
                        blog(LOG_INFO, "Found URL in config: %s", oldUrl.c_str());
                        
                        // Replace the URL
                        content.replace(valueStart, valueEnd - valueStart, newUrl);
                        updated = true;
                        
                        blog(LOG_INFO, "Updated URL field in config file from %s to %s", 
                             oldUrl.c_str(), newUrl.c_str());
                    }
                }
                
                // Then try server field
                size_t serverPos = content.find("\"server\":");
                if (serverPos != std::string::npos) {
                    // Find the beginning and end of the server value
                    size_t valueStart = content.find("\"", serverPos + 9) + 1;
                    size_t valueEnd = content.find("\"", valueStart);
                    
                    if (valueStart != std::string::npos && valueEnd != std::string::npos) {
                        std::string oldServer = content.substr(valueStart, valueEnd - valueStart);
                        blog(LOG_INFO, "Found server in config: %s", oldServer.c_str());
                        
                        // Replace the server
                        content.replace(valueStart, valueEnd - valueStart, newUrl);
                        updated = true;
                        
                        blog(LOG_INFO, "Updated server field in config file from %s to %s", 
                             oldServer.c_str(), newUrl.c_str());
                    }
                }
                
                // Write back to file if we made changes
                if (updated) {
                    std::ofstream outFile(path);
                    outFile << content;
                    outFile.close();
                    
                    blog(LOG_INFO, "Directly updated config file with new URL: %s", newUrl.c_str());
                    success = true;
                }
            }
        }
    }
    
    return success;
}

std::string SrtlaRelay::getCurrentOBSStreamServerURL() {
    std::string url = "";
    
    // Get the current streaming service
    obs_service_t* service = obs_frontend_get_streaming_service();
    if (service) {
        // Get current service settings
        obs_data_t* settings = obs_service_get_settings(service);
        if (settings) {
            // Log all settings for debugging - this will help understand structure
            blog(LOG_INFO, "Checking OBS service settings:");
            
            // Check 'server' field first as this is the primary field
            const char* server = obs_data_get_string(settings, "server");
            if (server && *server) {
                url = server;
                blog(LOG_INFO, "Found primary 'server' field: %s", url.c_str());
            }
            
            // Fall back to URL field if server not found
            if (url.empty()) {
                const char* urlCStr = obs_data_get_string(settings, "url");
                if (urlCStr && *urlCStr) {
                    url = urlCStr;
                    blog(LOG_INFO, "Found backup 'url' field: %s", url.c_str());
                }
            }
            
            // Also check service info and config
            const char* serviceType = obs_service_get_type(service);
            if (serviceType) {
                blog(LOG_INFO, "Service type: %s", serviceType);
            }
            
            obs_data_release(settings);
        }
    }
    
    // Try config file access if API fails or URL doesn't start with srt://
    if (url.empty() || url.compare(0, 6, "srt://") != 0) {
        blog(LOG_INFO, "API didn't provide usable SRT URL, trying config files and other approaches...");
        
        // Try all profiles directories
        const char* home = getenv("HOME");
        if (home) {
            // Get the basic profile directory
            std::string basicDir = std::string(home) + "/.config/obs-studio/basic";
            std::string profilesDir = basicDir + "/profiles";
            
            // Use current profile if possible
            char* currentProfile = obs_frontend_get_current_profile();
            if (currentProfile) {
                std::string profilePath = profilesDir + "/" + currentProfile;
                std::string servicePath = profilePath + "/service.json";
                
                blog(LOG_INFO, "Checking current profile service.json: %s", servicePath.c_str());
                
                if (fs::exists(servicePath)) {
                    blog(LOG_INFO, "Found current profile service config at: %s", servicePath.c_str());
                    
                    // Load the file using OBS API
                    obs_data_t* serviceData = obs_data_create_from_json_file(servicePath.c_str());
                    if (serviceData) {
                        // Save a copy for inspection
                        obs_data_save_json(serviceData, "/tmp/profile_service.json");
                        
                        // Try to get the URL from various possible places
                        const char* serviceUrl = obs_data_get_string(serviceData, "url");
                        if (serviceUrl && *serviceUrl) {
                            url = serviceUrl;
                            blog(LOG_INFO, "Found URL in service.json: %s", url.c_str());
                        } else {
                            // Try nested settings
                            obs_data_t* settings = obs_data_get_obj(serviceData, "settings");
                            if (settings) {
                                const char* settingsUrl = obs_data_get_string(settings, "url");
                                if (settingsUrl && *settingsUrl) {
                                    url = settingsUrl;
                                    blog(LOG_INFO, "Found URL in service.json settings: %s", url.c_str());
                                }
                                obs_data_release(settings);
                            }
                        }
                        
                        obs_data_release(serviceData);
                    }
                }
                
                bfree(currentProfile);
            }
            
            // If still no URL, try all profile directories
            if (url.empty() || url.compare(0, 6, "srt://") != 0) {
                // Try to enumerate directories in profiles folder
                if (fs::exists(profilesDir) && fs::is_directory(profilesDir)) {
                    for (const auto& entry : fs::directory_iterator(profilesDir)) {
                        if (fs::is_directory(entry.path())) {
                            std::string servicePath = entry.path().string() + "/service.json";
                            
                            if (fs::exists(servicePath)) {
                                blog(LOG_INFO, "Found service config in profile: %s", servicePath.c_str());
                                
                                // Read the file directly to look for SRT URL
                                std::ifstream file(servicePath);
                                std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                                file.close();
                                
                                // Look for "srt://" anywhere in the file
                                size_t pos = content.find("srt://");
                                if (pos != std::string::npos) {
                                    // Find the quotes around the URL
                                    size_t startQuote = content.rfind("\"", pos);
                                    size_t endQuote = content.find("\"", pos);
                                    
                                    if (startQuote != std::string::npos && endQuote != std::string::npos) {
                                        url = content.substr(startQuote + 1, endQuote - startQuote - 1);
                                        blog(LOG_INFO, "Found SRT URL in profile: %s", url.c_str());
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    blog(LOG_INFO, "Final current OBS stream server URL: %s", url.c_str());
    return url;
}

// Implementation of setLocalPort
void SrtlaRelay::setLocalPort(uint16_t port) {
    if (port != m_localPort) {
        m_localPort = port;
        blog(LOG_INFO, "Local port set to: %d", m_localPort);
        
        // Save settings immediately when port changes
        saveSettings();
        
        // If bidirectional sync is enabled, also update the OBS URL
        if (m_bidirectionalSync) {
            blog(LOG_INFO, "Bidirectional sync enabled - updating OBS URL with new port");
            std::string newUrl = buildSRTURL(port, m_latency, m_streamId);
            forceUpdateOBSStreamURL(newUrl);
        }
    }
}

// Implementation of setUseFixedPort
void SrtlaRelay::setUseFixedPort(bool enable) {
    if (enable != m_useFixedPort) {
        m_useFixedPort = enable;
        blog(LOG_INFO, "Fixed port mode set to: %s", enable ? "enabled" : "disabled");
        
        // Save settings immediately when fixed port setting changes
        saveSettings();
        
        // If bidirectional sync is enabled, also update the OBS URL
        if (m_bidirectionalSync && enable) {
            blog(LOG_INFO, "Updating OBS URL with fixed port mode");
            std::string newUrl = buildSRTURL(m_localPort, m_latency, m_streamId);
            forceUpdateOBSStreamURL(newUrl);
        }
    }
}

// Implementation of setServer
void SrtlaRelay::setServer(const std::string& server) {
    if (server != m_server) {
        m_server = server;
        blog(LOG_INFO, "SRTLA server set to: %s", server.c_str());
        
        // Save settings immediately when server changes
        saveSettings();
        
        // No need to update OBS URL as the server address is only for SRTLA
    }
}

// Implementation of setPort
void SrtlaRelay::setPort(uint16_t port) {
    if (port != m_port) {
        m_port = port;
        blog(LOG_INFO, "SRTLA port set to: %d", port);
        
        // Save settings immediately when port changes
        saveSettings();
        
        // No need to update OBS URL as this is the server port, not the local port
    }
}

// Implementation of setStreamId
void SrtlaRelay::setStreamId(const std::string& streamId) {
    if (streamId != m_streamId) {
        m_streamId = streamId;
        blog(LOG_INFO, "StreamID set to: %s", streamId.c_str());
        
        // Save settings immediately when streamId changes
        saveSettings();
        
        // If bidirectional sync is enabled, also update the OBS URL
        if (m_bidirectionalSync) {
            blog(LOG_INFO, "Bidirectional sync enabled - updating OBS URL with new streamId");
            std::string newUrl = buildSRTURL(m_localPort, m_latency, m_streamId);
            forceUpdateOBSStreamURL(newUrl);
        }
    }
}

// Implementation of setLatency
void SrtlaRelay::setLatency(int latency) {
    if (latency != m_latency) {
        m_latency = latency;
        blog(LOG_INFO, "Latency set to: %d ms", latency);
        
        // Save settings immediately when latency changes
        saveSettings();
        
        // If bidirectional sync is enabled, also update the OBS URL
        if (m_bidirectionalSync) {
            blog(LOG_INFO, "Bidirectional sync enabled - updating OBS URL with new latency");
            std::string newUrl = buildSRTURL(m_localPort, m_latency, m_streamId);
            forceUpdateOBSStreamURL(newUrl);
        }
    }
}

// Implementation of setAutoStart
void SrtlaRelay::setAutoStart(bool enable) {
    if (enable != m_autoStart) {
        m_autoStart = enable;
        blog(LOG_INFO, "Auto-start set to: %s", enable ? "enabled" : "disabled");
        
        // Save settings immediately when auto-start changes
        saveSettings();
    }
}

// Implementation of setBidirectionalSync
void SrtlaRelay::setBidirectionalSync(bool enable) {
    bool oldValue = m_bidirectionalSync;
    m_bidirectionalSync = enable;
    
    // Save settings first
    if (enable != oldValue) {
        saveSettings();
    }
    
    // If newly enabled, force fixed port mode and sync with OBS service
    if (enable && !oldValue) {
        blog(LOG_INFO, "Bidirectional sync enabled, syncing with OBS service");
        setUseFixedPort(true);
        
        // Force URL update to ensure it's persisted
        std::string newUrl = buildSRTURL(m_localPort, m_latency, m_streamId);
        forceUpdateOBSStreamURL(newUrl);
        
        // First add detailed debug of all OBS internal settings
        blog(LOG_INFO, "DEBUGGING ALL OBS SETTINGS STRUCTURES:");
        
        // Get the service
        obs_service_t* service = obs_frontend_get_streaming_service();
        if (service) {
            // Get current service settings
            obs_data_t* settings = obs_service_get_settings(service);
            if (settings) {
                // Log type and ID
                blog(LOG_INFO, "Service type: %s, ID: %s", 
                     obs_service_get_type(service), 
                     obs_service_get_id(service));
                     
                // Save the settings to a JSON file for inspection
                obs_data_save_json(settings, "/tmp/debug_service_settings.json");
                blog(LOG_INFO, "Saved service settings to /tmp/debug_service_settings.json");
                    
                // Try finding potential URL fields 
                const char* fields[] = {
                    "url", "server", "address", "hostname", "host", "stream", 
                    "srt_url", "service_url", "rtmp_url", "stream_url"
                };
                
                for (const char* field : fields) {
                    const char* value = obs_data_get_string(settings, field);
                    if (value && strlen(value) > 0) {
                        blog(LOG_INFO, "FOUND FIELD: %s = %s", field, value);
                    }
                }
                
                obs_data_release(settings);
            }
        }
        
        // Now try the normal sync
        syncFromOBSService();
        syncToOBSService();
    }
}

// Sync settings from OBS service to SRTLA
bool SrtlaRelay::syncFromOBSService() {
    blog(LOG_INFO, "Syncing settings from OBS service to SRTLA");
    
    // Store old values to report changes
    uint16_t oldLocalPort = m_localPort;
    int oldLatency = m_latency;
    std::string oldStreamId = m_streamId;
    
    obs_service_t* service = obs_frontend_get_streaming_service();
    if (!service) {
        blog(LOG_WARNING, "No active streaming service found");
        return false;
    }
    
    const char* service_id = obs_service_get_id(service);
    if (!service_id) {
        blog(LOG_WARNING, "Could not get service ID");
        return false;
    }
    
    blog(LOG_INFO, "Current service type: %s", service_id);
    
    // We want to use the Custom service, typically with ID "rtmp_custom"
    bool isCustom = (strcmp(service_id, "rtmp_custom") == 0);
    
    // Get the settings data
    obs_data_t* settings = obs_service_get_settings(service);
    if (!settings) {
        blog(LOG_WARNING, "Could not get service settings");
        return false;
    }
    
    // Extract URL and key from settings
    const char* url = obs_data_get_string(settings, "url");
    const char* key = obs_data_get_string(settings, "key");
    
    blog(LOG_INFO, "Service URL: %s, Key: %s", 
         url ? url : "NULL", 
         key ? key : "NULL");
    
    // Check if we need to switch to a custom service
    if (!isCustom && m_bidirectionalSync) {
        blog(LOG_INFO, "Bidirectional sync enabled but not using Custom service. Switching to Custom...");
        
        // Create a new Custom service with the same settings
        obs_data_t* customSettings = obs_data_create();
        
        // Copy existing settings if any
        if (url) obs_data_set_string(customSettings, "url", url);
        if (key) obs_data_set_string(customSettings, "key", key);
        
        // Create a Custom service
        obs_service_t* customService = obs_service_create("rtmp_custom", "Custom", customSettings, nullptr);
        if (customService) {
            // Set as the active service
            obs_frontend_set_streaming_service(customService);
            blog(LOG_INFO, "Switched to Custom service for bidirectional sync");
            
            // Add Qt notification for service switch
            QMetaObject::invokeMethod(QCoreApplication::instance(), []() {
                QMessageBox::information(nullptr, "Service Switched", 
                                        "Switched to Custom service for bidirectional sync.");
            }, Qt::QueuedConnection);
            
            // Update service pointer
            service = customService;
            
            // Release the service object
            obs_service_release(customService);
        } else {
            blog(LOG_ERROR, "Failed to create Custom service");
        }
        
        obs_data_release(customSettings);
    }
    
    if (!url || strlen(url) == 0) {
        // No URL configured, create a default SRT URL with our settings
        blog(LOG_INFO, "No URL configured in service. Creating default SRT URL");
        
        std::string newUrl = buildSRTURL(m_localPort, m_latency, m_streamId);
        
        // Get a copy of current settings to preserve other values
        obs_data_t* currentSettings = obs_service_get_settings(service);
        if (!currentSettings) {
            blog(LOG_ERROR, "Could not get current service settings");
            currentSettings = obs_data_create();
        }
        
        // Update the server field - this is the actual field name for OBS stream URL
        obs_data_set_string(currentSettings, "server", newUrl.c_str());
        
        // Also update url field just in case both are used
        obs_data_set_string(currentSettings, "url", newUrl.c_str());
        
        // Update the service with modified current settings
        obs_service_update(service, currentSettings);
        obs_data_release(currentSettings);
        
        // Force UI refresh by reapplying the service
        obs_frontend_set_streaming_service(service);
        
        // Save changes to the service.json file to ensure persistence
        const char* home = getenv("HOME");
        if (home) {
            char* currentProfile = obs_frontend_get_current_profile();
            if (currentProfile) {
                std::string servicePath = std::string(home) + 
                                         "/.config/obs-studio/basic/profiles/" + 
                                         currentProfile + 
                                         "/service.json";
                blog(LOG_INFO, "Updating service file directly: %s", servicePath.c_str());
                
                if (fs::exists(servicePath)) {
                    // Try to load the service file
                    obs_data_t* serviceConfig = obs_data_create_from_json_file(servicePath.c_str());
                    if (serviceConfig) {
                        // Get settings object
                        obs_data_t* settings = obs_data_get_obj(serviceConfig, "settings");
                        if (settings) {
                            // Update server field - this is the primary field in OBS for stream URL
                            obs_data_set_string(settings, "server", newUrl.c_str());
                            // Also update url field for compatibility
                            obs_data_set_string(settings, "url", newUrl.c_str());
                            obs_data_release(settings);
                        } else {
                            // Create settings if doesn't exist
                            settings = obs_data_create();
                            obs_data_set_string(settings, "server", newUrl.c_str());
                            obs_data_set_string(settings, "url", newUrl.c_str());
                            obs_data_set_obj(serviceConfig, "settings", settings);
                            obs_data_release(settings);
                        }
                        
                        // Save changes back to file
                        obs_data_save_json(serviceConfig, servicePath.c_str());
                        obs_data_release(serviceConfig);
                        blog(LOG_INFO, "Updated service.json file with new URL");
                        
                        // Force multiple refresh cycles to ensure changes appear immediately
                        // First cycle - update the settings and reapply
                        obs_data_t* refreshSettings = obs_service_get_settings(service);
                        if (refreshSettings) {
                            obs_data_set_string(refreshSettings, "server", newUrl.c_str());
                            obs_data_set_string(refreshSettings, "url", newUrl.c_str());
                            obs_service_update(service, refreshSettings);
                            obs_data_release(refreshSettings);
                        }
                        
                        // Force UI refresh by reapplying the service
                        obs_frontend_set_streaming_service(service);
                        
                        // Add a delay to allow changes to propagate
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        
                        // Second cycle - get fresh settings and update again
                        refreshSettings = obs_service_get_settings(service);
                        if (refreshSettings) {
                            obs_data_set_string(refreshSettings, "server", newUrl.c_str());
                            obs_data_set_string(refreshSettings, "url", newUrl.c_str());
                            obs_service_update(service, refreshSettings);
                            obs_data_release(refreshSettings);
                        }
                        
                        // Reapply the service again
                        obs_frontend_set_streaming_service(service);
                    }
                }
                bfree(currentProfile);
            }
        }
        
        // Add notification for URL update
        std::string urlCopy = newUrl;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [urlCopy]() {
            QString message = "Created default OBS Stream Server URL:\n\n";
            message += QString("%1\n\n").arg(QString::fromStdString(urlCopy));
            message += "This URL has been set in OBS Settings → Stream → Server.";
            
            QMessageBox::information(nullptr, "OBS Stream URL Created", message);
        }, Qt::QueuedConnection);
        
        blog(LOG_INFO, "Created default SRT URL and applied to service: %s", newUrl.c_str());
        
        obs_data_release(settings);
        return true;
    }
    
    // Check if URL is an SRT URL
    if (strncmp(url, "srt://", 6) != 0) {
        blog(LOG_INFO, "URL is not an SRT URL, converting to SRT format");
        
        // Convert to SRT format using current SRTLA settings
        std::string newUrl = buildSRTURL(m_localPort, m_latency, m_streamId);
        
        // Get a copy of current settings to preserve other values
        obs_data_t* currentSettings = obs_service_get_settings(service);
        if (!currentSettings) {
            blog(LOG_ERROR, "Could not get current service settings");
            currentSettings = obs_data_create();
        }
        
        // Update the server field - this is the actual field name for OBS stream URL
        obs_data_set_string(currentSettings, "server", newUrl.c_str());
        
        // Also update url field just in case both are used
        obs_data_set_string(currentSettings, "url", newUrl.c_str());
        
        // Update the service with modified current settings  
        obs_service_update(service, currentSettings);
        obs_data_release(currentSettings);
        
        // Force UI refresh by reapplying the service
        obs_frontend_set_streaming_service(service);
        
        // Add notification for URL conversion
        std::string urlCopy = url;
        std::string newUrlCopy = newUrl;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [urlCopy, newUrlCopy]() {
            QString message = "Converted OBS Stream Server URL to SRT format:\n\n";
            message += QString("Old: %1\n\n").arg(QString::fromStdString(urlCopy));
            message += QString("New: %2\n\n").arg(QString::fromStdString(newUrlCopy));
            message += "This change has been applied to OBS Settings → Stream → Server.";
            
            QMessageBox::information(nullptr, "OBS Stream URL Updated", message);
        }, Qt::QueuedConnection);
        
        blog(LOG_INFO, "Service URL was changed to SRT format: %s", newUrl.c_str());
        
        obs_data_release(settings);
        return true;
    }
    
    // Extract SRT parameters from URL
    uint16_t port;
    int latency;
    std::string streamId;
    
    blog(LOG_INFO, "Extracting parameters from SRT URL: %s", url);
    
    if (extractSRTParamsFromURL(url, port, latency, streamId)) {
        blog(LOG_INFO, "Successfully extracted SRT parameters - Port: %d, Latency: %d, StreamID: %s",
             port, latency, streamId.c_str());
        
        // Track what changed
        std::vector<std::string> changes;
        
        // Update local port (this is the main sync point)
        if (port > 0) {
            if (port != m_localPort) {
                blog(LOG_INFO, "Updating local port from %d to: %d", m_localPort, port);
                
                // Save port in settings immediately to ensure persistence
                m_localPort = port;
                saveSettings();
                
                setLocalPort(port);
                setUseFixedPort(true);
                changes.push_back("Local port: " + std::to_string(oldLocalPort) + " → " + std::to_string(port));
                
                // If already running, restart with new port
                if (isRunning()) {
                    blog(LOG_INFO, "Restarting SRTLA with new port");
                    restartWithPort(port);
                }
                
                // After setting port, immediately update OBS URL to reflect this change
                // This ensures the port setting is properly propagated
                std::string newUrl = buildSRTURL(port, m_latency, m_streamId);
                forceUpdateOBSStreamURL(newUrl);
                
            } else {
                blog(LOG_INFO, "Local port already matches OBS URL port: %d", port);
            }
        } else {
            blog(LOG_WARNING, "Invalid port (0) in URL, not updating local port");
            
            // Even with invalid port, still update the URL with the current port
            // to ensure service.json gets the right value
            std::string newUrl = buildSRTURL(m_localPort, m_latency, m_streamId);
            forceUpdateOBSStreamURL(newUrl);
        }
        
        // Update latency if specified in URL
        if (latency != 2000 && latency != m_latency) {  // 2000 is default, don't override if not specified
            blog(LOG_INFO, "Updating latency from %d to: %d", m_latency, latency);
            setLatency(latency);
            changes.push_back("Latency: " + std::to_string(oldLatency) + " → " + std::to_string(latency));
        }
        
        // Use streamId from URL
        if (!streamId.empty() && m_streamId != streamId) {
            blog(LOG_INFO, "Using stream ID from URL: %s", streamId.c_str());
            setStreamId(streamId);
            changes.push_back("Stream ID: '" + oldStreamId + "' → '" + streamId + "'");
        }
        // If URL doesn't have streamId but key field is set, just log it
        else if (key && *key) {
            blog(LOG_INFO, "Found stream key field (%s) but ignoring - SRT uses streamid in URL", key);
        }
        
        // Save the updated settings
        if (!changes.empty()) {
            blog(LOG_INFO, "Saving updated SRTLA settings");
            saveSettings();
            
            // Log the changes
            blog(LOG_INFO, "SRTLA settings updated to match service URL:");
            for (const auto& change : changes) {
                blog(LOG_INFO, "  %s", change.c_str());
            }
            
            // Add Qt notification for settings changes
            auto changesCopy = changes; // Make a copy for the lambda
            QMetaObject::invokeMethod(QCoreApplication::instance(), [changesCopy]() {
                QString message = "SRTLA settings updated:\n";
                for (const auto& change : changesCopy) {
                    message += QString::fromStdString(change) + "\n";
                }
                QMessageBox::information(nullptr, "SRTLA Settings Updated", message);
            }, Qt::QueuedConnection);
        } else {
            blog(LOG_INFO, "No changes needed, settings already match");
        }
        
        obs_data_release(settings);
        return !changes.empty();
    } else {
        blog(LOG_WARNING, "Failed to extract SRT parameters from URL: %s", url);
    }
    
    obs_data_release(settings);
    return false;
}

// Sync settings from SRTLA to OBS service
bool SrtlaRelay::syncToOBSService() {
    blog(LOG_INFO, "Syncing settings from SRTLA to OBS service");
    
    obs_service_t* service = obs_frontend_get_streaming_service();
    if (!service) {
        blog(LOG_WARNING, "No active streaming service found");
        return false;
    }
    
    const char* service_id = obs_service_get_id(service);
    if (!service_id) {
        blog(LOG_WARNING, "Could not get service ID");
        return false;
    }
    
    blog(LOG_INFO, "Current service type: %s", service_id);
    
    // We want to sync to the Custom service
    bool isCustom = (strcmp(service_id, "rtmp_custom") == 0);
    
    if (!isCustom && m_bidirectionalSync) {
        blog(LOG_INFO, "Bidirectional sync enabled but not using Custom service. Switching to Custom...");
        
        // Create a new Custom service with SRTLA settings
        obs_data_t* customSettings = obs_data_create();
        
        // Build URL from our settings - this is the crucial part we need in the stream server URL
        std::string url = buildSRTURL(m_localPort, m_latency, m_streamId);
        
        // Set URL (streamId is already included in the URL)
        obs_data_set_string(customSettings, "url", url.c_str());
        obs_data_set_string(customSettings, "key", ""); // Clear the key
        
        // Create a Custom service
        obs_service_t* customService = obs_service_create("rtmp_custom", "Custom", customSettings, nullptr);
        if (customService) {
            // Set as the active service
            obs_frontend_set_streaming_service(customService);
            blog(LOG_INFO, "Switched to Custom service with URL: %s", url.c_str());
            
            // Add Qt notification for service switch
            std::string urlCopy = url;
            QMetaObject::invokeMethod(QCoreApplication::instance(), [urlCopy]() {
                QMessageBox::information(nullptr, "Service Switched", 
                                        QString("Switched to Custom service with URL: %1")
                                        .arg(QString::fromStdString(urlCopy)));
            }, Qt::QueuedConnection);
            
            // Release the service object
            obs_service_release(customService);
        } else {
            blog(LOG_ERROR, "Failed to create Custom service");
        }
        
        obs_data_release(customSettings);
        return true;
    }
    
    // Get current service settings
    obs_data_t* settings = obs_service_get_settings(service);
    if (!settings) {
        blog(LOG_WARNING, "Could not get service settings");
        return false;
    }
    
    // Get current URL and key to see if we need to change anything
    const char* currentUrl = obs_data_get_string(settings, "url");
    const char* currentKey = obs_data_get_string(settings, "key");
    std::string url = currentUrl ? currentUrl : "";
    std::string key = currentKey ? currentKey : "";
    
    blog(LOG_INFO, "Current URL: %s, Key: %s", url.c_str(), key.c_str());
    
    // Make sure we're using the correct port that matches our current relay
    uint16_t usePort = m_localPort;
    if (isRunning() && m_bidirectionalSync) {
        blog(LOG_INFO, "Using the active relay port for URL: %d", usePort);
    } else {
        blog(LOG_INFO, "Using configured local port for URL: %d", usePort);
    }
    
    // Build the new URL based on SRTLA settings to match OBS UI format exactly
    std::string newUrl = buildSRTURL(usePort, m_latency, m_streamId);
    blog(LOG_INFO, "Built new SRT URL for OBS sync: %s", newUrl.c_str());
    
    // We're not using the key field for SRT anymore since streamId is in the URL
    std::string newKey = "";
    
    // Check if anything changed
    bool urlChanged = (url != newUrl);
    // Only consider key changes if the current key isn't empty
    bool keyChanged = !key.empty();
    
    if (urlChanged || keyChanged) {
        blog(LOG_INFO, "Service settings need updating:");
        if (urlChanged) blog(LOG_INFO, " - URL: %s → %s", url.c_str(), newUrl.c_str());
        if (keyChanged) blog(LOG_INFO, " - Key: %s → %s", key.c_str(), newKey.c_str());
        
        // Debug: dump all settings before update
        {
            obs_data_t* beforeSettings = obs_service_get_settings(service);
            obs_data_save_json(beforeSettings, "/tmp/service_before_update.json");
            blog(LOG_INFO, "Saved service settings BEFORE update to /tmp/service_before_update.json");
            obs_data_release(beforeSettings);
        }
        
        // Simpler approach: Just update the specific field in the current service settings
        blog(LOG_INFO, "Setting OBS Stream Server URL directly to: %s", newUrl.c_str());
        
        // Get the ID and name of the current service for info
        const char* currentId = obs_service_get_id(service);
        const char* currentName = obs_service_get_name(service);
        blog(LOG_INFO, "Current service - ID: %s, Name: %s", 
             currentId ? currentId : "NULL", 
             currentName ? currentName : "NULL");
        
        // Get a copy of current settings to preserve other values
        obs_data_t* currentSettings = obs_service_get_settings(service);
        if (!currentSettings) {
            blog(LOG_ERROR, "Could not get current service settings");
            currentSettings = obs_data_create();
        }
        
        // Update the server field - this is the primary field OBS uses for stream URL
        obs_data_set_string(currentSettings, "server", newUrl.c_str());
        
        // Also update url field just in case both are used
        obs_data_set_string(currentSettings, "url", newUrl.c_str());
        
        // Save settings to file for debugging
        obs_data_save_json(currentSettings, "/tmp/updated_service_settings.json");
        blog(LOG_INFO, "Saved updated service settings to /tmp/updated_service_settings.json");
         
        // Update the service with modified current settings
        obs_service_update(service, currentSettings);
        obs_data_release(currentSettings);
        
        // Force UI refresh by reapplying the service
        obs_frontend_set_streaming_service(service);
        
        // Add a larger delay to ensure OBS processes the update
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Debug: verify settings were applied
        obs_data_t* verifySettings = obs_service_get_settings(service);
        obs_data_save_json(verifySettings, "/tmp/service_after_update.json");
        blog(LOG_INFO, "Saved service settings AFTER update to /tmp/service_after_update.json");
        
        // Check specific fields
        const char* updatedUrl = obs_data_get_string(verifySettings, "url");
        const char* updatedServer = obs_data_get_string(verifySettings, "server");
        blog(LOG_INFO, "After update, URL field: %s", updatedUrl ? updatedUrl : "NULL");
        blog(LOG_INFO, "After update, server field: %s", updatedServer ? updatedServer : "NULL");
        obs_data_release(verifySettings);
        
        blog(LOG_INFO, "Successfully updated service URL to: %s", newUrl.c_str());
        
        // Log the changes
        blog(LOG_INFO, "OBS service settings updated to match SRTLA:");
        if (urlChanged) blog(LOG_INFO, "  URL: %s → %s", url.c_str(), newUrl.c_str());
        if (keyChanged) blog(LOG_INFO, "  Key: %s → %s", key.c_str(), newKey.c_str());
        
        // Add Qt notification for service settings update
        std::string urlCopy = url;
        std::string newUrlCopy = newUrl;
        
        QMetaObject::invokeMethod(QCoreApplication::instance(), [urlCopy, newUrlCopy]() {
            // Using a static timestamp in the lambda to prevent multiple notifications
            static auto lastNotificationTime = std::chrono::steady_clock::now() - std::chrono::seconds(10);
            auto now = std::chrono::steady_clock::now();
            
            // Only show notification if it's been more than 2 seconds since the last one
            if (std::chrono::duration_cast<std::chrono::seconds>(now - lastNotificationTime).count() >= 2) {
                QString message = "OBS Stream Server URL updated to match SRTLA settings:\n\n";
                message += QString("Old: %1\n\n").arg(QString::fromStdString(urlCopy));
                message += QString("New: %2\n\n").arg(QString::fromStdString(newUrlCopy));
                message += "This change has been applied to OBS Settings → Stream → Server.";
                
                QMessageBox::information(nullptr, "OBS Stream URL Updated", message);
                
                // Update the timestamp
                lastNotificationTime = now;
            }
        }, Qt::QueuedConnection);
        
        obs_data_release(settings);
        return true;
    } else {
        blog(LOG_INFO, "No changes needed, service settings already match SRTLA");
    }
    
    obs_data_release(settings);
    return false;
}

void SrtlaRelay::setupProperties() {
    // Service definitions are provided via the obs_service_info struct 
    // in the plugin registration
}

// Static callback when service info changes
void SrtlaRelay::serviceInfoChanged(void* data, calldata_t* cd) {
    SrtlaRelay* srtla = static_cast<SrtlaRelay*>(data);
    if (!srtla) return;
    
    obs_service_t* service = nullptr;
    if (!calldata_get_ptr(cd, "service", &service)) return;
    
    const char* service_id = obs_service_get_id(service);
    if (!service_id || strcmp(service_id, "srtla_service") != 0) {
        // Not our service, stop any running SRTLA process
        srtla->stopSrtlaProcess();
        return;
    }
    
    // Our service is selected, update settings and start SRTLA
    obs_data_t* settings = obs_service_get_settings(service);
    if (settings) {
        const char* server = obs_data_get_string(settings, "srtla_server");
        srtla->setServer(server ? server : "");
        
        srtla->setPort((uint16_t)obs_data_get_int(settings, "srtla_port"));
        
        const char* streamId = obs_data_get_string(settings, "srtla_stream_id"); 
        srtla->setStreamId(streamId ? streamId : "");
        
        srtla->saveSettings();
        
        // Start SRTLA process
        srtla->startSrtlaProcess();
        
        obs_data_release(settings);
    }
}

// Callbacks for OBS properties

// Called when SRTLA service is selected
static bool srtla_service_selected(obs_properties_t *props, obs_property_t *property, obs_data_t *settings) {
    const char *service = obs_data_get_string(settings, "service");
    bool isSrtla = service && strcmp(service, "srtla_service") == 0;
    
    // Toggle visibility of SRTLA-specific settings
    obs_property_t *server = obs_properties_get(props, "srtla_server");
    obs_property_t *port = obs_properties_get(props, "srtla_port");
    obs_property_t *streamId = obs_properties_get(props, "srtla_stream_id");
    obs_property_t *apply = obs_properties_get(props, "apply_srtla");
    
    if (server) obs_property_set_visible(server, isSrtla);
    if (port) obs_property_set_visible(port, isSrtla);
    if (streamId) obs_property_set_visible(streamId, isSrtla);
    if (apply) obs_property_set_visible(apply, isSrtla);
    
    // Hide standard RTMP settings if SRTLA is selected
    obs_property_t *url = obs_properties_get(props, "url");
    obs_property_t *key = obs_properties_get(props, "key");
    
    if (url) obs_property_set_visible(url, !isSrtla);
    if (key) obs_property_set_visible(key, !isSrtla);
    
    return true;
}

// Apply button callback
static bool apply_srtla_settings(obs_properties_t *props, obs_property_t *property, void *data) {
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);
    
    SrtlaRelay *srtla = static_cast<SrtlaRelay *>(data);
    if (!srtla) return false;
    
    srtla->saveSettings();
    
    // If already running, restart the process
    if (srtla->isRunning()) {
        srtla->stopSrtlaProcess();
        srtla->startSrtlaProcess();
    }
    
    return true;
}

// OBS service type definition
static const char *srtla_service_getname(void *type_data) {
    UNUSED_PARAMETER(type_data);
    return "SRTLA Relay";
}

static void *srtla_service_create(obs_data_t *settings, obs_service_t *service) {
    UNUSED_PARAMETER(service);
    
    blog(LOG_INFO, "Creating SRTLA service");
    
    // Just return the settings data
    obs_data_addref(settings);
    return settings;
}

static void srtla_service_destroy(void *data) {
    obs_data_t *settings = (obs_data_t *)data;
    if (settings) {
        obs_data_release(settings);
    }
}

static void srtla_service_update(void *data, obs_data_t *settings) {
    UNUSED_PARAMETER(data);
    
    // This is called when service settings are updated
    // Get the service settings and update SRTLA relay
    const char *server = obs_data_get_string(settings, "server");
    uint16_t port = (uint16_t)obs_data_get_int(settings, "port");
    const char *stream_id = obs_data_get_string(settings, "stream_id");
    
    blog(LOG_INFO, "SRTLA service settings updated: server=%s, port=%d, stream_id=%s", 
         server, port, stream_id);
    
    extern SrtlaRelay *g_srtlaRelay; // Declare the global instance
    if (g_srtlaRelay) {
        g_srtlaRelay->setServer(server);
        g_srtlaRelay->setPort(port);
        g_srtlaRelay->setStreamId(stream_id);
        g_srtlaRelay->saveSettings();
    }
}

static void srtla_service_get_defaults(obs_data_t *settings) {
    obs_data_set_default_string(settings, "server", "");
    obs_data_set_default_int(settings, "port", 3000);
    obs_data_set_default_string(settings, "stream_id", "");
}

static obs_properties_t *srtla_service_get_properties(void *data) {
    UNUSED_PARAMETER(data);
    
    blog(LOG_INFO, "Getting SRTLA service properties");
    
    obs_properties_t *props = obs_properties_create();
    
    // Add a service type dropdown - this seems to be required for services to appear in UI
    obs_property_t *p = obs_properties_add_list(props, "service", "Service", 
                                OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    
    // Add only our service to the dropdown
    obs_property_list_add_string(p, "SRTLA Relay", "SRTLA Relay");
    
    // SRTLA specific settings
    obs_properties_add_text(props, "server", "SRTLA Server", OBS_TEXT_DEFAULT);
    obs_properties_add_int(props, "port", "SRTLA Port", 1, 65535, 1);
    obs_properties_add_text(props, "stream_id", "Stream ID (Optional)", OBS_TEXT_DEFAULT);
    
    return props;
}

static bool srtla_service_initialize(void *data, obs_output_t *output) {
    UNUSED_PARAMETER(output);
    obs_data_t *settings = (obs_data_t *)data;
    
    blog(LOG_INFO, "Initializing SRTLA service");
    
    // Debug dump of service settings
    obs_data_save_json(settings, "/tmp/service_initialize_settings.json");
    blog(LOG_INFO, "Saved service initialization settings to /tmp/service_initialize_settings.json");
    
    // List all fields in the settings
    blog(LOG_INFO, "Service initialization settings fields:");
    const char* fields[] = {
        "url", "server", "port", "stream_id", "streamid", "key"
    };
    for (const char* field : fields) {
        const char* str_value = obs_data_get_string(settings, field);
        long long int_value = obs_data_get_int(settings, field);
        if (str_value && *str_value) {
            blog(LOG_INFO, "  %s (string): %s", field, str_value);
        }
        if (int_value != 0) {
            blog(LOG_INFO, "  %s (int): %lld", field, int_value);
        }
    }
    
    // Start SRTLA relay service when streaming is initiated
    extern SrtlaRelay *g_srtlaRelay; // Declare the global instance
    if (g_srtlaRelay) {
        // Get settings from the service
        const char *server = obs_data_get_string(settings, "server");
        uint16_t port = (uint16_t)obs_data_get_int(settings, "port");
        const char *stream_id = obs_data_get_string(settings, "stream_id");
        
        // Try getting URL directly from settings
        const char *url = obs_data_get_string(settings, "url");
        if (url && *url) {
            blog(LOG_INFO, "Found URL in service init: %s", url);
            
            // If it's an SRT URL, extract parameters
            if (strncmp(url, "srt://", 6) == 0) {
                uint16_t srtPort;
                int latency;
                std::string srtStreamId;
                
                if (g_srtlaRelay->extractSRTParamsFromURL(url, srtPort, latency, srtStreamId)) {
                    blog(LOG_INFO, "Extracted from URL - port: %d, streamId: %s", 
                         srtPort, srtStreamId.c_str());
                         
                    // Use parameters from URL
                    if (srtPort > 0) {
                        port = srtPort;
                    }
                    if (!srtStreamId.empty()) {
                        stream_id = srtStreamId.c_str();
                    }
                }
            }
        }
        
        blog(LOG_INFO, "Starting SRTLA with: server=%s, port=%d, stream_id=%s", 
             server ? server : "NULL", port, stream_id ? stream_id : "NULL");
        
        // Update SRTLA relay settings
        g_srtlaRelay->setServer(server);
        g_srtlaRelay->setPort(port);
        g_srtlaRelay->setStreamId(stream_id);
        
        // Start SRTLA relay process
        if (g_srtlaRelay->startSrtlaProcess()) {
            blog(LOG_INFO, "SRTLA relay started successfully");
        } else {
            blog(LOG_ERROR, "Failed to start SRTLA relay");
        }
    }
    
    return true;
}

static const char *srtla_service_get_url(void *data) {
    UNUSED_PARAMETER(data);
    
    blog(LOG_INFO, "***** IMPORTANT! srtla_service_get_url called *****");
    
    // Debug: dump the passed data object if present
    if (data) {
        blog(LOG_INFO, "Data object provided to get_url");
        obs_data_t* settings = (obs_data_t*)data;
        obs_data_save_json(settings, "/tmp/get_url_data.json");
        blog(LOG_INFO, "Saved get_url data to /tmp/get_url_data.json");
        
        // Check for url field
        const char* urlInData = obs_data_get_string(settings, "url");
        if (urlInData && *urlInData) {
            blog(LOG_INFO, "URL found in data: %s", urlInData);
        }
    }
    
    // Get local port from SRTLA relay instance
    extern SrtlaRelay *g_srtlaRelay; // Declare the global instance
    uint16_t localPort = 0;
    int latency = 2000; // Default latency
    std::string streamId;
    
    if (g_srtlaRelay) {
        localPort = g_srtlaRelay->getLocalPort();
        latency = g_srtlaRelay->getLatency();
        streamId = g_srtlaRelay->getStreamId();
        
        blog(LOG_INFO, "Using SRTLA relay settings: port=%d, latency=%d, streamId=%s", 
             localPort, latency, streamId.c_str());
    } else {
        // Default port if plugin instance not available
        localPort = 10000;
        blog(LOG_INFO, "SRTLA relay instance not available, using default port: %d", localPort);
    }
    
    static std::string url;
    
    // Build the URL in the exact format OBS expects (srt://localhost:PORT?streamid=ID&latency=VALUE)
    url = std::string("srt://localhost:") + std::to_string(localPort);
    
    // Include both streamId and latency parameters
    bool hasParam = false;
    
    // Add streamId parameter first
    if (!streamId.empty()) {
        url += "?streamid=" + streamId;
        hasParam = true;
    }
    
    // Always add latency parameter when in valid range
    if (latency >= 1000 && latency <= 8000) {
        url += (hasParam ? "&" : "?") + std::string("latency=") + std::to_string(latency);
    }
    
    blog(LOG_INFO, "***** SRTLA service returning URL: %s *****", url.c_str());
    return url.c_str();
}

static const char *srtla_service_get_key(void *data) {
    obs_data_t *settings = (obs_data_t *)data;
    const char *stream_id = obs_data_get_string(settings, "stream_id");
    
    // If stream_id is specified, use it as the key
    // SRT will use this as the streamid in the URL
    return stream_id ? stream_id : "";
}

static const char *srtla_service_get_protocol(void *data) {
    UNUSED_PARAMETER(data);
    return "SRT";  // Return the protocol name
}

struct obs_service_info srtla_service = {
    // Create a unique ID for our service
    .id = "srtla_service", 
    .get_name = srtla_service_getname,
    .create = srtla_service_create,
    .destroy = srtla_service_destroy,
    
    // Optional fields
    .activate = nullptr,
    .deactivate = nullptr,
    .update = srtla_service_update,
    .get_defaults = srtla_service_get_defaults,
    .get_properties = srtla_service_get_properties,
    .initialize = srtla_service_initialize,
    .get_url = srtla_service_get_url,
    .get_key = srtla_service_get_key,
    .get_username = nullptr,
    .get_password = nullptr,
    .deprecated_1 = nullptr,
    .apply_encoder_settings = nullptr,
    .type_data = nullptr,
    .free_type_data = nullptr,
    .get_output_type = nullptr,
    .get_supported_resolutions = nullptr,
    .get_max_fps = nullptr,
    .get_max_bitrate = nullptr,
    .get_supported_video_codecs = nullptr,
    .get_protocol = srtla_service_get_protocol
};