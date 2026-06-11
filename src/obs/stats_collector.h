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

#ifndef OBS_SRTLA_STATS_COLLECTOR_H
#define OBS_SRTLA_STATS_COLLECTOR_H

#include "../core/balancer.h"
#include <functional>

/*
 * StatsCollector - Collects OBS output statistics for bitrate adaptation
 *
 * This class polls OBS output statistics periodically and invokes
 * a callback with the collected data formatted as BalancerInput.
 */
class StatsCollector {
public:
    using StatsCallback = std::function<void(const BalancerInput&)>;

    StatsCollector();
    ~StatsCollector();

    // Set callback to receive stats updates
    void setCallback(StatsCallback callback);

    // Start collecting stats at the specified interval (ms)
    void start(int interval_ms);

    // Stop collecting stats
    void stop();

    // Check if currently collecting
    bool isRunning() const { return m_running; }

    // Get last collected input (for debugging)
    BalancerInput getLastInput() const { return m_lastInput; }

private:
    // Timer callback
    static void timerCallback(void *param);
    
    // Collect current stats from OBS
    void collectStats();

    StatsCallback m_callback;
    bool m_running;
    int m_interval_ms;
    
    // Last collected values for delta calculation
    int m_prevDroppedFrames;
    uint64_t m_prevTimestamp;
    BalancerInput m_lastInput;
};

#endif /* OBS_SRTLA_STATS_COLLECTOR_H */
