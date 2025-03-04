#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <mutex>

struct NetworkInterface {
    std::string name;
    std::string ipAddress;
    bool isWireless;
    bool isEthernet;
    bool isModem;
    bool isActive;
};

class NetworkMonitor {
public:
    NetworkMonitor();
    ~NetworkMonitor();

    // Start monitoring network interfaces
    void start();
    
    // Stop monitoring network interfaces
    void stop();
    
    // Get current network interfaces
    std::vector<NetworkInterface> getNetworkInterfaces();
    
    // Save IP list to file
    bool saveIpListToFile(const std::string& filePath);
    
    // Register callback for network changes
    using NetworkChangeCallback = std::function<void(const std::vector<NetworkInterface>&)>;
    void registerCallback(NetworkChangeCallback callback);
    
    // Detect network interfaces - made public so it can be called directly
    std::vector<NetworkInterface> detectNetworkInterfaces();

private:
    bool m_running;
    std::vector<NetworkInterface> m_interfaces;
    std::mutex m_mutex;
    std::vector<NetworkChangeCallback> m_callbacks;
    
    // Thread function to monitor network changes
    void monitorThread();
    
    // Compare interfaces to detect changes
    bool haveInterfacesChanged(const std::vector<NetworkInterface>& oldInterfaces, 
                              const std::vector<NetworkInterface>& newInterfaces);
    
    // Notify all registered callbacks
    void notifyNetworkChange();
};