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

#ifndef OBS_SRTLA_BALANCER_H
#define OBS_SRTLA_BALANCER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Balancer configuration - passed to init()
 * All bitrate values are in Kbps for OBS compatibility
 */
typedef struct {
    int min_bitrate;        // Minimum allowed bitrate (Kbps)
    int max_bitrate;        // Maximum allowed bitrate (Kbps)
    int incr_step;          // Bitrate increase step (Kbps, default: 100)
    int decr_step;          // Bitrate decrease step (Kbps, default: 200)
    int incr_interval;      // Min interval between increases (ms, default: 500)
    int decr_interval;      // Min interval between decreases (ms, default: 100)
    int update_interval;    // Update polling interval (ms, default: 100)

    // OBS-specific thresholds
    float congestion_high;  // High congestion threshold (0.0-1.0, default: 0.7)
    float congestion_low;   // Low congestion threshold (0.0-1.0, default: 0.2)
    int dropped_threshold;  // Dropped frames per interval threshold (default: 5)
} BalancerConfig;

/*
 * Balancer input - passed to step() every update cycle
 * Uses OBS output statistics instead of SRT stats
 */
typedef struct {
    float congestion;           // OBS congestion value (0.0 - 1.0)
    int dropped_frames;         // Cumulative dropped frames
    int dropped_frames_delta;   // Dropped frames since last update
    uint64_t total_bytes;       // Total bytes sent
    uint64_t timestamp;         // Current timestamp (ms)
} BalancerInput;

/*
 * Balancer output - returned from step()
 */
typedef struct {
    int new_bitrate;            // Computed bitrate (Kbps)
    const char *reason;         // Human-readable reason for change (debug)
    int changed;                // Whether bitrate changed from previous
    
    // Debug info for overlay/logging
    float congestion;           // Current congestion value
    int dropped_delta;          // Dropped frames this interval
    int current_bitrate;        // Bitrate before this update
} BalancerOutput;

/*
 * Balancer algorithm interface
 *
 * Each algorithm implements these three functions:
 * - init:    Allocate and initialize algorithm state
 * - step:    Compute new bitrate based on current OBS stats
 * - cleanup: Free algorithm state
 */
typedef struct {
    const char *name;           // Algorithm name (e.g., "obs_adaptive", "fixed")
    const char *description;    // Human-readable description

    // Initialize algorithm state, returns opaque state pointer
    void* (*init)(const BalancerConfig *config);

    // Compute new bitrate, returns output with bitrate and debug info
    BalancerOutput (*step)(void *state, const BalancerInput *input);

    // Clean up algorithm state
    void (*cleanup)(void *state);
} BalancerAlgorithm;

/*
 * Registry functions
 */

// Get the default algorithm (used when not specified)
const BalancerAlgorithm* balancer_get_default(void);

// Find algorithm by name, returns NULL if not found
const BalancerAlgorithm* balancer_find(const char *name);

// Get array of all registered algorithms (NULL-terminated)
const BalancerAlgorithm* const* balancer_list_all(void);

// Get default configuration values
void balancer_config_defaults(BalancerConfig *config);

#ifdef __cplusplus
}
#endif

#endif /* OBS_SRTLA_BALANCER_H */
