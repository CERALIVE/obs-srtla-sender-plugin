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

#ifndef OBS_SRTLA_BALANCER_RUNNER_H
#define OBS_SRTLA_BALANCER_RUNNER_H

#include "balancer.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Balancer runner module - orchestrates balancer algorithm execution
 *
 * This module initializes and manages the balancer algorithm lifecycle,
 * providing a clean interface for updating bitrate based on OBS stats.
 */

typedef struct {
    const BalancerAlgorithm *algo;
    void *state;
    BalancerConfig config;
    int enabled;            // Whether dynamic bitrate is enabled
    int current_bitrate;    // Last known bitrate (Kbps)
} BalancerRunner;

/*
 * Initialize balancer runner with configuration
 *
 * @param runner        Runner instance to initialize
 * @param config        Balancer configuration
 * @param algo_name     Algorithm name (NULL for default)
 *
 * Returns 0 on success, < 0 on error.
 */
int balancer_runner_init(BalancerRunner *runner, const BalancerConfig *config,
                         const char *algo_name);

/*
 * Update bitrate based on OBS statistics
 *
 * This is called periodically to compute new bitrate.
 * Returns BalancerOutput with new bitrate and debug info.
 */
BalancerOutput balancer_runner_step(BalancerRunner *runner, const BalancerInput *input);

/*
 * Update min/max bitrate bounds
 */
void balancer_runner_update_bounds(BalancerRunner *runner, int min_bitrate, int max_bitrate);

/*
 * Update algorithm (reinitializes state)
 */
int balancer_runner_set_algorithm(BalancerRunner *runner, const char *algo_name);

/*
 * Enable or disable dynamic bitrate
 */
void balancer_runner_set_enabled(BalancerRunner *runner, int enabled);

/*
 * Check if dynamic bitrate is enabled
 */
int balancer_runner_is_enabled(const BalancerRunner *runner);

/*
 * Get current algorithm name
 */
const char* balancer_runner_get_name(const BalancerRunner *runner);

/*
 * Get current bitrate
 */
int balancer_runner_get_bitrate(const BalancerRunner *runner);

/*
 * Cleanup balancer runner
 */
void balancer_runner_cleanup(BalancerRunner *runner);

#ifdef __cplusplus
}
#endif

#endif /* OBS_SRTLA_BALANCER_RUNNER_H */
