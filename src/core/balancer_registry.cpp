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

/*
 * Balancer registry - manages available algorithms
 */

#include "balancer.h"
#include <cstring>

/*
 * External algorithm definitions
 */
extern const BalancerAlgorithm balancer_obs_adaptive;
extern const BalancerAlgorithm balancer_fixed;
extern const BalancerAlgorithm balancer_aggressive;

/*
 * Registry of all available algorithms
 * First entry is the default
 */
static const BalancerAlgorithm* const algorithms[] = {
    &balancer_obs_adaptive,
    &balancer_fixed,
    &balancer_aggressive,
    nullptr  // Sentinel
};

/*
 * Get the default algorithm (first in registry)
 */
const BalancerAlgorithm* balancer_get_default(void) {
    return algorithms[0];
}

/*
 * Find algorithm by name
 */
const BalancerAlgorithm* balancer_find(const char *name) {
    if (name == nullptr) {
        return nullptr;
    }

    for (int i = 0; algorithms[i] != nullptr; i++) {
        if (strcmp(algorithms[i]->name, name) == 0) {
            return algorithms[i];
        }
    }

    return nullptr;
}

/*
 * Get array of all registered algorithms
 */
const BalancerAlgorithm* const* balancer_list_all(void) {
    return algorithms;
}

/*
 * Get default configuration values
 */
void balancer_config_defaults(BalancerConfig *config) {
    config->min_bitrate = 500;       // 500 Kbps minimum
    config->max_bitrate = 6000;      // 6000 Kbps default max
    config->incr_step = 100;         // 100 Kbps increase step
    config->decr_step = 200;         // 200 Kbps decrease step
    config->incr_interval = 500;     // 500ms between increases
    config->decr_interval = 100;     // 100ms between decreases
    config->update_interval = 100;   // 100ms update polling
    config->congestion_high = 0.7f;  // 70% congestion = decrease
    config->congestion_low = 0.2f;   // 20% congestion = can increase
    config->dropped_threshold = 5;   // 5 dropped frames = problem
}
