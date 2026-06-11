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

#include "bitrate_manager.h"
#include <obs-module.h>
#include <util/config-file.h>
#include <cstring>

// Global instance
BitrateManager* g_bitrateManager = nullptr;

// Settings file path
static const char* BITRATE_SETTINGS_FILE = "srtla_bitrate_settings.json";

BitrateManager::BitrateManager()
    : m_running(false)
    , m_initialized(false)
{
    m_lastOutput = {0};
    balancer_config_defaults(&m_config);
    
    m_statsCollector = std::make_unique<StatsCollector>();
    m_encoderController = std::make_unique<EncoderController>();
}

BitrateManager::~BitrateManager() {
    stop();
    balancer_runner_cleanup(&m_runner);
}

bool BitrateManager::init(const BalancerConfig &config, const char *algo_name) {
    m_config = config;
    
    int ret = balancer_runner_init(&m_runner, &m_config, algo_name);
    if (ret < 0) {
        blog(LOG_ERROR, "[SRTLA Bitrate] Failed to initialize balancer runner");
        return false;
    }

    // Set up stats collector callback
    m_statsCollector->setCallback([this](const BalancerInput &input) {
        this->onStats(input);
    });

    m_initialized = true;
    blog(LOG_INFO, "[SRTLA Bitrate] Manager initialized with algorithm: %s",
         balancer_runner_get_name(&m_runner));
    
    return true;
}

void BitrateManager::start() {
    if (m_running || !m_initialized) {
        return;
    }

    // Check if encoder supports runtime bitrate changes
    if (!m_encoderController->supportsRuntimeBitrate()) {
        blog(LOG_WARNING, "[SRTLA Bitrate] Encoder may not support runtime bitrate changes");
    }

    // Get initial bitrate from encoder
    int current = m_encoderController->getBitrate();
    if (current > 0) {
        m_runner.current_bitrate = current;
        blog(LOG_INFO, "[SRTLA Bitrate] Initial encoder bitrate: %d Kbps", current);
    }

    // Start collecting stats
    m_statsCollector->start(m_config.update_interval);
    m_running = true;

    blog(LOG_INFO, "[SRTLA Bitrate] Started dynamic bitrate control");
}

void BitrateManager::stop() {
    if (!m_running) {
        return;
    }

    m_statsCollector->stop();
    m_running = false;

    blog(LOG_INFO, "[SRTLA Bitrate] Stopped dynamic bitrate control");
}

void BitrateManager::setEnabled(bool enabled) {
    balancer_runner_set_enabled(&m_runner, enabled ? 1 : 0);
}

bool BitrateManager::isEnabled() const {
    return balancer_runner_is_enabled(&m_runner) != 0;
}

void BitrateManager::setMinBitrate(int kbps) {
    m_config.min_bitrate = kbps;
    balancer_runner_update_bounds(&m_runner, m_config.min_bitrate, m_config.max_bitrate);
}

void BitrateManager::setMaxBitrate(int kbps) {
    m_config.max_bitrate = kbps;
    balancer_runner_update_bounds(&m_runner, m_config.min_bitrate, m_config.max_bitrate);
}

void BitrateManager::setAlgorithm(const std::string &name) {
    int ret = balancer_runner_set_algorithm(&m_runner, name.c_str());
    if (ret < 0) {
        blog(LOG_WARNING, "[SRTLA Bitrate] Failed to set algorithm: %s", name.c_str());
    }
}

void BitrateManager::setUpdateInterval(int ms) {
    m_config.update_interval = ms;
    if (m_running) {
        m_statsCollector->stop();
        m_statsCollector->start(ms);
    }
}

int BitrateManager::getCurrentBitrate() const {
    return balancer_runner_get_bitrate(&m_runner);
}

std::string BitrateManager::getCurrentAlgorithm() const {
    const char *name = balancer_runner_get_name(&m_runner);
    return name ? std::string(name) : "none";
}

void BitrateManager::onStats(const BalancerInput &input) {
    if (!m_initialized || !m_running) {
        return;
    }

    // Run balancer algorithm
    BalancerOutput output = balancer_runner_step(&m_runner, &input);
    m_lastOutput = output;

    // Update encoder if bitrate changed
    if (output.changed) {
        bool success = m_encoderController->setBitrate(output.new_bitrate);
        if (!success) {
            blog(LOG_WARNING, "[SRTLA Bitrate] Failed to set encoder bitrate: %s",
                 m_encoderController->getLastError().c_str());
        } else {
            blog(LOG_DEBUG, "[SRTLA Bitrate] Bitrate: %d -> %d Kbps (%s)",
                 output.current_bitrate, output.new_bitrate, output.reason);
        }
    }
}

void BitrateManager::saveSettings() {
    char *config_dir = obs_module_config_path("");
    if (!config_dir) {
        blog(LOG_WARNING, "[SRTLA Bitrate] Could not get config directory");
        return;
    }

    std::string path = std::string(config_dir) + BITRATE_SETTINGS_FILE;
    bfree(config_dir);

    obs_data_t *data = obs_data_create();
    
    obs_data_set_bool(data, "enabled", isEnabled());
    obs_data_set_int(data, "min_bitrate", m_config.min_bitrate);
    obs_data_set_int(data, "max_bitrate", m_config.max_bitrate);
    obs_data_set_int(data, "incr_step", m_config.incr_step);
    obs_data_set_int(data, "decr_step", m_config.decr_step);
    obs_data_set_int(data, "incr_interval", m_config.incr_interval);
    obs_data_set_int(data, "decr_interval", m_config.decr_interval);
    obs_data_set_int(data, "update_interval", m_config.update_interval);
    obs_data_set_double(data, "congestion_high", m_config.congestion_high);
    obs_data_set_double(data, "congestion_low", m_config.congestion_low);
    obs_data_set_int(data, "dropped_threshold", m_config.dropped_threshold);
    obs_data_set_string(data, "algorithm", getCurrentAlgorithm().c_str());

    obs_data_save_json(data, path.c_str());
    obs_data_release(data);

    blog(LOG_INFO, "[SRTLA Bitrate] Settings saved to %s", path.c_str());
}

void BitrateManager::loadSettings() {
    char *config_dir = obs_module_config_path("");
    if (!config_dir) {
        return;
    }

    std::string path = std::string(config_dir) + BITRATE_SETTINGS_FILE;
    bfree(config_dir);

    obs_data_t *data = obs_data_create_from_json_file(path.c_str());
    if (!data) {
        blog(LOG_INFO, "[SRTLA Bitrate] No settings file found, using defaults");
        return;
    }

    // Load configuration
    if (obs_data_has_user_value(data, "min_bitrate")) {
        m_config.min_bitrate = (int)obs_data_get_int(data, "min_bitrate");
    }
    if (obs_data_has_user_value(data, "max_bitrate")) {
        m_config.max_bitrate = (int)obs_data_get_int(data, "max_bitrate");
    }
    if (obs_data_has_user_value(data, "incr_step")) {
        m_config.incr_step = (int)obs_data_get_int(data, "incr_step");
    }
    if (obs_data_has_user_value(data, "decr_step")) {
        m_config.decr_step = (int)obs_data_get_int(data, "decr_step");
    }
    if (obs_data_has_user_value(data, "incr_interval")) {
        m_config.incr_interval = (int)obs_data_get_int(data, "incr_interval");
    }
    if (obs_data_has_user_value(data, "decr_interval")) {
        m_config.decr_interval = (int)obs_data_get_int(data, "decr_interval");
    }
    if (obs_data_has_user_value(data, "update_interval")) {
        m_config.update_interval = (int)obs_data_get_int(data, "update_interval");
    }
    if (obs_data_has_user_value(data, "congestion_high")) {
        m_config.congestion_high = (float)obs_data_get_double(data, "congestion_high");
    }
    if (obs_data_has_user_value(data, "congestion_low")) {
        m_config.congestion_low = (float)obs_data_get_double(data, "congestion_low");
    }
    if (obs_data_has_user_value(data, "dropped_threshold")) {
        m_config.dropped_threshold = (int)obs_data_get_int(data, "dropped_threshold");
    }

    // Initialize with loaded config
    const char *algo = obs_data_get_string(data, "algorithm");
    if (algo && algo[0] != '\0') {
        init(m_config, algo);
    } else {
        init(m_config, nullptr);
    }

    // Set enabled state
    if (obs_data_has_user_value(data, "enabled")) {
        setEnabled(obs_data_get_bool(data, "enabled"));
    }

    obs_data_release(data);
    blog(LOG_INFO, "[SRTLA Bitrate] Settings loaded from %s", path.c_str());
}
