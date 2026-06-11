/*
    OBS SRTLA Sender Plugin - Dynamic Bitrate Control
    Copyright (C) 2026 CERALIVE

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef OBS_SRTLA_BITRATE_MANAGER_H
#define OBS_SRTLA_BITRATE_MANAGER_H

#include "../core/balancer.h"
#include "../core/balancer_runner.h"
#include "stats_collector.h"
#include "encoder_controller.h"
#include <memory>
#include <string>

/*
 * BitrateManager - Main orchestrator for dynamic bitrate control
 *
 * This class coordinates the StatsCollector, BalancerRunner, and
 * EncoderController to provide automatic bitrate adaptation.
 */
class BitrateManager {
public:
    BitrateManager();
    ~BitrateManager();

    // Initialize with configuration
    bool init(const BalancerConfig &config, const char *algo_name = nullptr);

    // Start/stop dynamic bitrate adaptation
    void start();
    void stop();

    // Check if running
    bool isRunning() const { return m_running; }

    // Enable/disable (can be toggled while running)
    void setEnabled(bool enabled);
    bool isEnabled() const;

    // Configuration updates
    void setMinBitrate(int kbps);
    void setMaxBitrate(int kbps);
    void setAlgorithm(const std::string &name);
    void setUpdateInterval(int ms);

    // Get current state
    int getCurrentBitrate() const;
    std::string getCurrentAlgorithm() const;
    BalancerConfig getConfig() const { return m_config; }

    // Get last balancer output (for UI/debugging)
    BalancerOutput getLastOutput() const { return m_lastOutput; }

    // Check if initialized
    bool isInitialized() const { return m_initialized; }

    // Save/load settings
    void saveSettings();
    void loadSettings();

private:
    // Called by StatsCollector with new stats
    void onStats(const BalancerInput &input);

    BalancerRunner m_runner;
    std::unique_ptr<StatsCollector> m_statsCollector;
    std::unique_ptr<EncoderController> m_encoderController;
    
    BalancerConfig m_config;
    BalancerOutput m_lastOutput;
    
    bool m_running;
    bool m_initialized;
};

// Global instance
extern BitrateManager* g_bitrateManager;

#endif /* OBS_SRTLA_BITRATE_MANAGER_H */
