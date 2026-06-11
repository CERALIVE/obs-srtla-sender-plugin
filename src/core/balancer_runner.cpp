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

#include "balancer_runner.h"
#include <obs-module.h>
#include <cstdlib>
#include <cstring>

int balancer_runner_init(BalancerRunner *runner, const BalancerConfig *config,
                         const char *algo_name) {
    runner->algo = nullptr;
    runner->state = nullptr;
    runner->enabled = 1;
    runner->current_bitrate = config->max_bitrate;

    // Copy configuration
    memcpy(&runner->config, config, sizeof(BalancerConfig));

    // Select algorithm
    if (algo_name != nullptr && algo_name[0] != '\0') {
        runner->algo = balancer_find(algo_name);
        if (runner->algo == nullptr) {
            blog(LOG_WARNING, "[SRTLA Bitrate] Unknown algorithm: %s, using default", algo_name);
        }
    }
    
    if (runner->algo == nullptr) {
        runner->algo = balancer_get_default();
    }

    blog(LOG_INFO, "[SRTLA Bitrate] Algorithm: %s", runner->algo->name);

    // Initialize the algorithm
    runner->state = runner->algo->init(&runner->config);
    if (runner->state == nullptr) {
        blog(LOG_ERROR, "[SRTLA Bitrate] Failed to initialize algorithm");
        return -1;
    }

    blog(LOG_INFO, "[SRTLA Bitrate] Range: %d - %d Kbps",
         runner->config.min_bitrate, runner->config.max_bitrate);

    return 0;
}

BalancerOutput balancer_runner_step(BalancerRunner *runner, const BalancerInput *input) {
    BalancerOutput output = {0};
    
    if (!runner->enabled || runner->algo == nullptr || runner->state == nullptr) {
        output.new_bitrate = runner->current_bitrate;
        output.reason = "disabled";
        output.changed = 0;
        return output;
    }

    output = runner->algo->step(runner->state, input);
    
    // Track if bitrate actually changed
    output.changed = (output.new_bitrate != runner->current_bitrate) ? 1 : 0;
    output.current_bitrate = runner->current_bitrate;
    
    if (output.changed) {
        runner->current_bitrate = output.new_bitrate;
    }

    return output;
}

void balancer_runner_update_bounds(BalancerRunner *runner, int min_bitrate, int max_bitrate) {
    runner->config.min_bitrate = min_bitrate;
    runner->config.max_bitrate = max_bitrate;

    // Reinitialize algorithm with new config
    if (runner->algo != nullptr && runner->state != nullptr) {
        runner->algo->cleanup(runner->state);
        runner->state = runner->algo->init(&runner->config);
        
        // Clamp current bitrate to new bounds
        if (runner->current_bitrate < min_bitrate) {
            runner->current_bitrate = min_bitrate;
        } else if (runner->current_bitrate > max_bitrate) {
            runner->current_bitrate = max_bitrate;
        }
    }
}

int balancer_runner_set_algorithm(BalancerRunner *runner, const char *algo_name) {
    const BalancerAlgorithm *new_algo = balancer_find(algo_name);
    if (new_algo == nullptr) {
        return -1;
    }

    // Cleanup old state
    if (runner->algo != nullptr && runner->state != nullptr) {
        runner->algo->cleanup(runner->state);
    }

    // Initialize new algorithm
    runner->algo = new_algo;
    runner->state = runner->algo->init(&runner->config);
    
    if (runner->state == nullptr) {
        return -2;
    }

    blog(LOG_INFO, "[SRTLA Bitrate] Switched to algorithm: %s", runner->algo->name);
    return 0;
}

void balancer_runner_set_enabled(BalancerRunner *runner, int enabled) {
    runner->enabled = enabled;
    blog(LOG_INFO, "[SRTLA Bitrate] Dynamic bitrate %s", enabled ? "enabled" : "disabled");
}

int balancer_runner_is_enabled(const BalancerRunner *runner) {
    return runner->enabled;
}

const char* balancer_runner_get_name(const BalancerRunner *runner) {
    return runner->algo ? runner->algo->name : "none";
}

int balancer_runner_get_bitrate(const BalancerRunner *runner) {
    return runner->current_bitrate;
}

void balancer_runner_cleanup(BalancerRunner *runner) {
    if (runner->algo != nullptr && runner->state != nullptr) {
        runner->algo->cleanup(runner->state);
        runner->state = nullptr;
    }
    runner->algo = nullptr;
}
