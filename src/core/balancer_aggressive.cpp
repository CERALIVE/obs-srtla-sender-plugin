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
 * Aggressive balancer - faster reactions for unstable connections
 *
 * This algorithm is designed for highly variable network conditions
 * where quick reactions are more important than bitrate stability.
 * It decreases faster and increases slower than the default adaptive.
 */

#include "balancer.h"
#include <cstdlib>

/*
 * State structure
 */
struct AggressiveState {
    BalancerConfig config;
    
    int current_bitrate;        // Current bitrate (Kbps)
    uint64_t last_increase;     // Timestamp of last increase
    uint64_t last_decrease;     // Timestamp of last decrease
    
    // Fast reaction tracking
    int consecutive_bad;        // Consecutive bad intervals
    int consecutive_good;       // Consecutive good intervals
};

/*
 * Clamp bitrate to configured bounds
 */
static int clamp_bitrate(int bitrate, const BalancerConfig *config) {
    if (bitrate < config->min_bitrate) {
        return config->min_bitrate;
    } else if (bitrate > config->max_bitrate) {
        return config->max_bitrate;
    }
    return bitrate;
}

/*
 * Initialize the aggressive balancer
 */
static void* aggressive_init(const BalancerConfig *config) {
    AggressiveState *state = (AggressiveState*)malloc(sizeof(AggressiveState));
    if (state == nullptr) {
        return nullptr;
    }

    state->config = *config;
    state->current_bitrate = config->max_bitrate;
    state->last_increase = 0;
    state->last_decrease = 0;
    state->consecutive_bad = 0;
    state->consecutive_good = 0;

    return state;
}

/*
 * Compute new bitrate with aggressive adaptation
 */
static BalancerOutput aggressive_step(void *state_ptr, const BalancerInput *input) {
    AggressiveState *state = (AggressiveState *)state_ptr;
    BalancerOutput output = {0};
    
    int new_bitrate = state->current_bitrate;
    const char *reason = "stable";
    
    // Any congestion > 0.5 or any dropped frames = immediate decrease
    bool is_bad = (input->congestion > 0.5f) || (input->dropped_frames_delta > 0);
    bool is_good = (input->congestion < 0.1f) && (input->dropped_frames_delta == 0);
    
    if (is_bad) {
        state->consecutive_bad++;
        state->consecutive_good = 0;
        
        // Decrease immediately, more aggressively with consecutive bad intervals
        int multiplier = (state->consecutive_bad > 3) ? 3 : state->consecutive_bad;
        int decrease = state->config.decr_step * multiplier;
        
        // Add percentage-based decrease for faster convergence
        decrease += state->current_bitrate / 5;  // 20% of current
        
        new_bitrate = state->current_bitrate - decrease;
        reason = "aggressive_decrease";
        state->last_decrease = input->timestamp;
    }
    else if (is_good) {
        state->consecutive_good++;
        state->consecutive_bad = 0;
        
        // Only increase after many consecutive good intervals
        // and with significant delay
        if (state->consecutive_good >= 10 &&
            input->timestamp - state->last_increase >= (uint64_t)state->config.incr_interval * 2) {
            
            // Small increase
            new_bitrate = state->current_bitrate + state->config.incr_step / 2;
            reason = "cautious_increase";
            state->last_increase = input->timestamp;
        }
    }
    else {
        // Neither good nor bad - reset counters
        state->consecutive_bad = 0;
        state->consecutive_good = 0;
    }
    
    // Apply bounds
    new_bitrate = clamp_bitrate(new_bitrate, &state->config);
    
    // Fill output
    output.new_bitrate = new_bitrate;
    output.reason = reason;
    output.changed = (new_bitrate != state->current_bitrate) ? 1 : 0;
    output.congestion = input->congestion;
    output.dropped_delta = input->dropped_frames_delta;
    output.current_bitrate = state->current_bitrate;
    
    state->current_bitrate = new_bitrate;
    
    return output;
}

/*
 * Clean up aggressive balancer state
 */
static void aggressive_cleanup(void *state_ptr) {
    free(state_ptr);
}

/*
 * Aggressive balancer algorithm definition
 */
extern "C" const BalancerAlgorithm balancer_aggressive = {
    .name = "aggressive",
    .description = "Fast reactions for unstable connections",
    .init = aggressive_init,
    .step = aggressive_step,
    .cleanup = aggressive_cleanup,
};
