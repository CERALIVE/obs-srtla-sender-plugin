#include "network-monitor.h"
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

#include <unistd.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

NetworkMonitor::NetworkMonitor() : m_running(false) {
}

NetworkMonitor::~NetworkMonitor() {
    stop();
}

void NetworkMonitor::start() {
    if (m_running) return;
    
    m_running = true;
    std::thread t(&NetworkMonitor::monitorThread, this);
    t.detach();
}

void NetworkMonitor::stop() {
    m_running = false;
}

std::vector<NetworkInterface> NetworkMonitor::getNetworkInterfaces() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_interfaces;
}

bool NetworkMonitor::saveIpListToFile(const std::string& filePath) {
    // Force a fresh detection of network interfaces
    std::vector<NetworkInterface> interfaces = detectNetworkInterfaces();
    
    // Store the updated interfaces
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_interfaces = interfaces;
    }
    
    std::ofstream file(filePath);
    if (!file.is_open()) {
        return false;
    }
    
    // Log the interfaces we found
    std::string foundIps;
    bool atLeastOneIp = false;
    
    for (const auto& interface : interfaces) {
        // Skip localhost/loopback
        if (interface.ipAddress == "127.0.0.1" || interface.name == "lo") {
            continue;
        }
        
        // Only include active interfaces with valid IPs
        if (interface.isActive && !interface.ipAddress.empty()) {
            file << interface.ipAddress << "\n";
            foundIps += interface.name + "(" + interface.ipAddress + ") ";
            atLeastOneIp = true;
        }
    }
    
    // If no valid interfaces were found, don't write anything
    // This is because SRTLA will use all available interfaces if no IP file is provided
    if (!atLeastOneIp) {
        std::cout << "No non-loopback network interfaces found" << std::endl;
        // Return true because this isn't considered an error
        return true;
    }
    
    // Log what we found
    std::cout << "Found network interfaces: " << foundIps << std::endl;
    
    return true;
}

void NetworkMonitor::registerCallback(NetworkChangeCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callbacks.push_back(callback);
}

void NetworkMonitor::monitorThread() {
    std::vector<NetworkInterface> lastInterfaces;
    
    while (m_running) {
        std::vector<NetworkInterface> currentInterfaces = detectNetworkInterfaces();
        
        // Only rewrite the IP file if actual IP changes are detected
        if (haveInterfacesChanged(lastInterfaces, currentInterfaces)) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_interfaces = currentInterfaces;
            }
            notifyNetworkChange(); // This will rewrite the IP file and send HUP signal
        }
        
        lastInterfaces = currentInterfaces;
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

std::vector<NetworkInterface> NetworkMonitor::detectNetworkInterfaces() {
    std::vector<NetworkInterface> interfaces;
    
    // Linux implementation
    struct ifaddrs* ifaddr;
    if (getifaddrs(&ifaddr) == -1) {
        return interfaces;
    }
    
    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        
        // Only consider IPv4 addresses
        if (ifa->ifa_addr->sa_family == AF_INET) {
            NetworkInterface interface;
            interface.name = ifa->ifa_name;
            interface.isActive = (ifa->ifa_flags & IFF_UP) && (ifa->ifa_flags & IFF_RUNNING);
            
            // Determine interface type based on name (common naming conventions)
            interface.isEthernet = (interface.name.find("eth") == 0 || 
                                   interface.name.find("en") == 0 ||
                                   interface.name.find("eno") == 0 ||
                                   interface.name.find("enp") == 0);
            
            interface.isWireless = (interface.name.find("wlan") == 0 || 
                                   interface.name.find("wifi") == 0 ||
                                   interface.name.find("wl") == 0);
            
            interface.isModem = (interface.name.find("ppp") == 0 || 
                                interface.name.find("tun") == 0 ||
                                interface.name.find("tap") == 0);
            
            // Skip loopback interfaces
            if (interface.name == "lo" || ifa->ifa_flags & IFF_LOOPBACK) continue;
            
            // Get IP address
            char ipStr[INET_ADDRSTRLEN];
            struct sockaddr_in* addr = (struct sockaddr_in*)ifa->ifa_addr;
            inet_ntop(AF_INET, &(addr->sin_addr), ipStr, INET_ADDRSTRLEN);
            interface.ipAddress = ipStr;
            
            interfaces.push_back(interface);
        }
    }
    
    freeifaddrs(ifaddr);

    return interfaces;
}

bool NetworkMonitor::haveInterfacesChanged(const std::vector<NetworkInterface>& oldInterfaces, 
                                         const std::vector<NetworkInterface>& newInterfaces) {
    // Extract only active, non-loopback interfaces with valid IPs
    std::vector<std::string> oldIps;
    std::vector<std::string> newIps;
    
    for (const auto& iface : oldInterfaces) {
        if (iface.isActive && !iface.ipAddress.empty() && iface.ipAddress != "127.0.0.1" && iface.name != "lo") {
            oldIps.push_back(iface.ipAddress);
        }
    }
    
    for (const auto& iface : newInterfaces) {
        if (iface.isActive && !iface.ipAddress.empty() && iface.ipAddress != "127.0.0.1" && iface.name != "lo") {
            newIps.push_back(iface.ipAddress);
        }
    }
    
    // Sort IPs for reliable comparison
    std::sort(oldIps.begin(), oldIps.end());
    std::sort(newIps.begin(), newIps.end());
    
    // Compare IP sets - if different, interfaces have changed
    if (oldIps.size() != newIps.size()) {
        return true;
    }
    
    for (size_t i = 0; i < oldIps.size(); i++) {
        if (oldIps[i] != newIps[i]) {
            return true;
        }
    }
    
    return false;
}

void NetworkMonitor::notifyNetworkChange() {
    std::vector<NetworkInterface> interfaces;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        interfaces = m_interfaces;
        
        for (const auto& callback : m_callbacks) {
            callback(interfaces);
        }
    }
}