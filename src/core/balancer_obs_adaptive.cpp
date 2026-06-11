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
 * OBS Adaptive balancer - congestion and dropped frames based bitrate control
 *
 * This is the default algorithm that adapts bitrate based on:
 * - OBS congestion value (0.0 - 1.0)
 * - Dropped frames count
 *
 * It uses multiple congestion detection thresholds to provide
 * graduated responses from gentle decreases to emergency drops.
 */

#include "balancer.h"
#include <cstdlib>
#include <algorithm>

/*
 * State structure
 */
struct OBSAdaptiveState {
    BalancerConfig config;
    
    // Current state
    int current_bitrate;        // Current bitrate (Kbps)
    uint64_t last_increase;     // Timestamp of last increase
    uint64_t last_decrease;     // Timestamp of last decrease
    
    // Smoothed metrics (EMA)
    float congestion_avg;       // Smoothed congestion
    float dropped_rate;         // Dropped frames per second
    
    // Tracking
    int prev_dropped_frames;    // Previous total dropped frames
    uint64_t prev_timestamp;    // Previous timestamp
    
    // Stability tracking
    int stable_count;           // Consecutive stable intervals
};

// EMA smoothing factors
static const float EMA_CONGESTION = 0.3f;   // Faster response to congestion
static const float EMA_DROPPED = 0.2f;      // Slightly slower for dropped frames

// Stability thresholds
static const int STABLE_INTERVALS_FOR_INCREASE = 5;  // 500ms of stability before increase

/*
 * Clamp bitrate to configured bounds and round to 50 Kbps
 */
static int clamp_and_round(int bitrate, const BalancerConfig *config) {
    // Round to nearest 50 Kbps
    bitrate = (bitrate / 50) * 50;
    
    // Clamp to bounds
    if (bitrate < config->min_bitrate) {
        bitrate = config->min_bitrate;
    } else if (bitrate > config->max_bitrate) {
        bitrate = config->max_bitrate;
    }
    
    return bitrate;
}

/*
 * Initialize the OBS adaptive balancer
 */
static void* obs_adaptive_init(const BalancerConfig *config) {
    OBSAdaptiveState *state = (OBSAdaptiveState*)malloc(sizeof(OBSAdaptiveState));
    if (state == nullptr) {
        return nullptr;
    }

    state->config = *config;
    state->current_bitrate = config->max_bitrate;
    state->last_increase = 0;
    state->last_decrease = 0;
    state->congestion_avg = 0.0f;
    state->dropped_rate = 0.0f;
    state->prev_dropped_frames = 0;
    state->prev_timestamp = 0;
    state->stable_count = 0;

    return state;
}

/*
 * Compute new bitrate based on current OBS stats
 */
static BalancerOutput obs_adaptive_step(void *state_ptr, const BalancerInput *input) {
    OBSAdaptiveState *state = (OBSAdaptiveState *)state_ptr;
    BalancerOutput output = {0};
    
    // Update smoothed congestion
    state->congestion_avg = state->congestion_avg * (1.0f - EMA_CONGESTION) 
                          + input->congestion * EMA_CONGESTION;
    
    // Calculate dropped frames rate
    if (state->prev_timestamp > 0 && input->timestamp > state->prev_timestamp) {
        uint64_t dt = input->timestamp - state->prev_timestamp;
        if (dt > 0) {
            int dropped_delta = input->dropped_frames - state->prev_dropped_frames;
            if (dropped_delta < 0) dropped_delta = 0;  // Handle counter reset
            
            float rate = (float)dropped_delta * 1000.0f / (float)dt;  // per second
            state->dropped_rate = state->dropped_rate * (1.0f - EMA_DROPPED) 
                                + rate * EMA_DROPPED;
        }
    }
    
    state->prev_dropped_frames = input->dropped_frames;
    state->prev_timestamp = input->timestamp;
    
    // Decision logic
    int new_bitrate = state->current_bitrate;
    const char *reason = "stable";
    
    // Emergency: Very high congestion - drop immediately to minimum
    if (input->congestion > 0.9f) {
        new_bitrate = state->config.min_bitrate;
        reason = "emergency";
        state->stable_count = 0;
        state->last_decrease = input->timestamp;
    }
    // High congestion or many dropped frames - decrease significantly
    else if (state->congestion_avg > state->config.congestion_high || 
             input->dropped_frames_delta > state->config.dropped_threshold * 2) {
        
        if (input->timestamp - state->last_decrease >= (uint64_t)state->config.decr_interval) {
            // Larger decrease based on severity
            int decrease = state->config.decr_step;
            if (state->congestion_avg > 0.8f) {
                decrease = decrease * 2;  // Double decrease for severe congestion
            }
            decrease += state->current_bitrate / 10;  // Add 10% of current
            
            new_bitrate = state->current_bitrate - decrease;
            reason = "high_congestion";
            state->stable_count = 0;
            state->last_decrease = input->timestamp;
        }
    }
    // Moderate congestion or some dropped frames - decrease moderately
    else if (state->congestion_avg > 0.5f || 
             input->dropped_frames_delta > state->config.dropped_threshold) {
        
        if (input->timestamp - state->last_decrease >= (uint64_t)state->config.decr_interval) {
            new_bitrate = state->current_bitrate - state->config.decr_step;
            reason = "moderate_congestion";
            state->stable_count = 0;
            state->last_decrease = input->timestamp;
        }
    }
    // Low congestion, no drops - can increase
    else if (state->congestion_avg < state->config.congestion_low && 
             input->dropped_frames_delta == 0 &&
             state->dropped_rate < 0.5f) {
        
        state->stable_count++;
        
        if (state->stable_count >= STABLE_INTERVALS_FOR_INCREASE &&
            input->timestamp - state->last_increase >= (uint64_t)state->config.incr_interval) {
            
            // Gradual increase
            int increase = state->config.incr_step;
            increase += state->current_bitrate / 30;  // Add ~3% of current
            
            new_bitrate = state->current_bitrate + increase;
            reason = "increasing";
            state->last_increase = input->timestamp;
            // Don't reset stable_count to allow continuous increase
        }
    } else {
        // Neither good nor bad - just waiting
        state->stable_count = 0;
    }
    
    // Apply bounds and rounding
    new_bitrate = clamp_and_round(new_bitrate, &state->config);
    
    // Fill output
    output.new_bitrate = new_bitrate;
    output.reason = reason;
    output.changed = (new_bitrate != state->current_bitrate) ? 1 : 0;
    output.congestion = state->congestion_avg;
    output.dropped_delta = input->dropped_frames_delta;
    output.current_bitrate = state->current_bitrate;
    
    // Update state
    state->current_bitrate = new_bitrate;
    
    return output;
}

/*
 * Clean up OBS adaptive balancer state
 */
static void obs_adaptive_cleanup(void *state_ptr) {
    free(state_ptr);
}

/*
 * OBS Adaptive balancer algorithm definition
 */
extern "C" const BalancerAlgorithm balancer_obs_adaptive = {
    .name = "obs_adaptive",
    .description = "OBS congestion and dropped frames based adaptive control (default)",
    .init = obs_adaptive_init,
    .step = obs_adaptive_step,
    .cleanup = obs_adaptive_cleanup,
};
