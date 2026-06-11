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

#include "stats_collector.h"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/threading.h>
#include <util/platform.h>
#include <chrono>
#include <thread>

StatsCollector::StatsCollector()
    : m_callback(nullptr)
    , m_running(false)
    , m_interval_ms(100)
    , m_prevDroppedFrames(0)
    , m_prevTimestamp(0)
{
    m_lastInput = {0};
}

StatsCollector::~StatsCollector() {
    stop();
}

void StatsCollector::setCallback(StatsCallback callback) {
    m_callback = callback;
}

void StatsCollector::start(int interval_ms) {
    if (m_running) {
        return;
    }

    m_interval_ms = interval_ms;
    m_running = true;
    m_prevDroppedFrames = 0;
    m_prevTimestamp = 0;

    // Schedule first timer tick
    obs_queue_task(OBS_TASK_UI, timerCallback, this, false);
    
    blog(LOG_INFO, "[SRTLA Stats] Started collecting stats every %d ms", interval_ms);
}

void StatsCollector::stop() {
    if (!m_running) {
        return;
    }

    m_running = false;
    blog(LOG_INFO, "[SRTLA Stats] Stopped collecting stats");
}

void StatsCollector::timerCallback(void *param) {
    StatsCollector *collector = static_cast<StatsCollector*>(param);
    
    if (!collector->m_running) {
        return;
    }

    collector->collectStats();

    // Schedule next tick using a thread sleep
    if (collector->m_running) {
        // Use a separate thread for the delay to avoid blocking the UI
        std::thread([collector]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(collector->m_interval_ms));
            if (collector->m_running) {
                obs_queue_task(OBS_TASK_UI, timerCallback, collector, false);
            }
        }).detach();
    }
}

void StatsCollector::collectStats() {
    obs_output_t *output = obs_frontend_get_streaming_output();
    if (!output) {
        return;
    }

    // Get current timestamp
    auto now = std::chrono::steady_clock::now();
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();

    // Collect stats from OBS output
    float congestion = obs_output_get_congestion(output);
    int total_frames = obs_output_get_total_frames(output);
    int frames_dropped = obs_output_get_frames_dropped(output);
    uint64_t total_bytes = obs_output_get_total_bytes(output);

    // Calculate dropped frames delta
    int dropped_delta = 0;
    if (m_prevTimestamp > 0) {
        dropped_delta = frames_dropped - m_prevDroppedFrames;
        if (dropped_delta < 0) {
            dropped_delta = 0;  // Handle counter reset
        }
    }

    // Build input struct
    BalancerInput input = {0};
    input.congestion = congestion;
    input.dropped_frames = frames_dropped;
    input.dropped_frames_delta = dropped_delta;
    input.total_bytes = total_bytes;
    input.timestamp = timestamp;

    // Update tracking
    m_prevDroppedFrames = frames_dropped;
    m_prevTimestamp = timestamp;
    m_lastInput = input;

    // Release output reference
    obs_output_release(output);

    // Invoke callback
    if (m_callback) {
        m_callback(input);
    }
}
