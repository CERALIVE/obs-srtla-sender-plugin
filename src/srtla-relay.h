#pragma once

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <string>
#include <memory>
#include <vector>
#include "network-monitor.h"

#define SRTLA_PLUGIN_NAME "SRTLA Relay"

// Forward declare the service info structure
extern struct obs_service_info srtla_service;

class SrtlaRelay {
public:
    SrtlaRelay();
    ~SrtlaRelay();

    // Initialize the plugin
    void init();
    
    // Save plugin settings
    // Returns true if the settings file already existed
    bool saveSettings();
    
    // Load plugin settings
    void loadSettings();
    
    // Get server settings
    std::string getServer() const { return m_server; }
    uint16_t getPort() const { return m_port; }
    std::string getStreamId() const { return m_streamId; }
    
    // Set server settings
    void setServer(const std::string& server);  // Implementation in cpp file
    void setPort(uint16_t port);  // Implementation in cpp file
    void setStreamId(const std::string& streamId);  // Implementation in cpp file
    
    // Get/set auto-start flag
    bool isAutoStartEnabled() const { return m_autoStart; }
    void setAutoStart(bool enable);  // Implementation in cpp file
    
    // Get/set bidirectional sync flag
    bool isBidirectionalSyncEnabled() const { return m_bidirectionalSync; }
    void setBidirectionalSync(bool enable);  // Implementation in cpp file
    
    // Get/set latency
    int getLatency() const { return m_latency; }
    void setLatency(int latency);  // Implementation in cpp file
    
    // Sync settings between OBS service and SRTLA relay
    bool syncFromOBSService();
    bool syncToOBSService();
    
    // Get the current OBS stream server URL
    std::string getCurrentOBSStreamServerURL();
    
    // Parse and build SRT URLs
    bool extractSRTParamsFromURL(const std::string& url, uint16_t& port, int& latency, std::string& streamId);
    std::string buildSRTURL(uint16_t port, int latency, const std::string& streamId);
    
    // Force update of OBS stream URL in all possible locations
    bool forceUpdateOBSStreamURL(const std::string& newUrl);
    
    // Start/stop SRTLA process
    bool startSrtlaProcess();
    void stopSrtlaProcess();
    
    // Restart the process with a specific port
    bool restartWithPort(uint16_t port);
    
    // Handle network changes
    void onNetworkChange(const std::vector<NetworkInterface>& interfaces);
    
    // Check if SRTLA process is running
    bool isRunning() const { return m_processRunning; }
    
    // Generate random port
    uint16_t generateRandomPort() const;
    
    // Get/set local port
    uint16_t getLocalPort() const { return m_localPort; }
    void setLocalPort(uint16_t port);  // Implementation in cpp file
    
    // Use a fixed local port instead of random
    bool isFixedPortEnabled() const { return m_useFixedPort; }
    void setUseFixedPort(bool enable);  // Implementation in cpp file
    
    // Get IP list file path
    std::string getIpListPath() const { return m_ipListPath; }

private:
    // Settings
    std::string m_server;
    uint16_t m_port;
    std::string m_streamId;
    bool m_autoStart;      // Flag to auto-start SRTLA with streaming
    bool m_bidirectionalSync; // Flag to sync between OBS and SRTLA
    int m_latency;         // SRT latency in milliseconds
    
    // Network monitor
    std::unique_ptr<NetworkMonitor> m_networkMonitor;
    
    // Local port for SRT
    uint16_t m_localPort;
    bool m_useFixedPort;  // Whether to use fixed port or random port
    
    // IP list file path
    std::string m_ipListPath;
    
    // Process handling
    bool m_processRunning;
    int m_processId;
    
    // Kill SRTLA process if running
    void killSrtlaProcess();
    
    // Setup UI properties
    void setupProperties();
    
    // Service info modified callback
    static void serviceInfoChanged(void* data, calldata_t* cd);
};

// Global instance of SrtlaRelay (accessible to external modules)
extern SrtlaRelay* g_srtlaRelay;