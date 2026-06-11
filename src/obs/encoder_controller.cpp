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

#include "encoder_controller.h"
#include <obs-frontend-api.h>

EncoderController::EncoderController()
    : m_lastBitrate(-1)
    , m_bitrateProperty("")
{
}

EncoderController::~EncoderController() {
}

obs_encoder_t* EncoderController::getStreamingEncoder() {
    obs_output_t *output = obs_frontend_get_streaming_output();
    if (!output) {
        m_lastError = "No streaming output available";
        return nullptr;
    }

    obs_encoder_t *encoder = obs_output_get_video_encoder(output);
    obs_output_release(output);

    if (!encoder) {
        m_lastError = "No video encoder on streaming output";
        return nullptr;
    }

    return encoder;
}

std::string EncoderController::detectBitrateProperty(obs_encoder_t *encoder) {
    // Common bitrate property names across different encoders
    static const char* bitrate_properties[] = {
        "bitrate",          // Most common (x264, NVENC, etc.)
        "rate_control",     // Some encoders use this
        "target_bitrate",   // Some hardware encoders
        "vbitrate",         // Video-specific bitrate
        nullptr
    };

    obs_data_t *settings = obs_encoder_get_settings(encoder);
    if (!settings) {
        return "bitrate";  // Default fallback
    }

    for (int i = 0; bitrate_properties[i] != nullptr; i++) {
        if (obs_data_has_user_value(settings, bitrate_properties[i])) {
            obs_data_release(settings);
            return bitrate_properties[i];
        }
    }

    obs_data_release(settings);
    return "bitrate";  // Default
}

bool EncoderController::setBitrate(int bitrate_kbps) {
    obs_encoder_t *encoder = getStreamingEncoder();
    if (!encoder) {
        return false;
    }

    // Skip if bitrate hasn't changed
    if (bitrate_kbps == m_lastBitrate) {
        return true;
    }

    // Detect property name if not cached
    if (m_bitrateProperty.empty()) {
        m_bitrateProperty = detectBitrateProperty(encoder);
    }

    // Get current settings
    obs_data_t *settings = obs_encoder_get_settings(encoder);
    if (!settings) {
        m_lastError = "Failed to get encoder settings";
        return false;
    }

    // Update bitrate
    obs_data_set_int(settings, m_bitrateProperty.c_str(), bitrate_kbps);

    // Apply the update
    obs_encoder_update(encoder, settings);
    obs_data_release(settings);

    blog(LOG_INFO, "[SRTLA Encoder] Bitrate updated: %d -> %d Kbps", 
         m_lastBitrate, bitrate_kbps);
    
    m_lastBitrate = bitrate_kbps;
    m_lastError = "";
    
    return true;
}

int EncoderController::getBitrate() {
    obs_encoder_t *encoder = getStreamingEncoder();
    if (!encoder) {
        return -1;
    }

    // Detect property name if not cached
    if (m_bitrateProperty.empty()) {
        m_bitrateProperty = detectBitrateProperty(encoder);
    }

    obs_data_t *settings = obs_encoder_get_settings(encoder);
    if (!settings) {
        return -1;
    }

    int bitrate = (int)obs_data_get_int(settings, m_bitrateProperty.c_str());
    obs_data_release(settings);

    return bitrate;
}

bool EncoderController::supportsRuntimeBitrate() {
    obs_encoder_t *encoder = getStreamingEncoder();
    if (!encoder) {
        return false;
    }

    // Most software encoders support runtime bitrate changes
    // Hardware encoders may have limitations
    const char *id = obs_encoder_get_id(encoder);
    if (!id) {
        return true;  // Assume yes if unknown
    }

    // Known encoders that support runtime bitrate changes
    static const char* supported_encoders[] = {
        "obs_x264",
        "ffmpeg_nvenc",
        "jim_nvenc",
        "obs_qsv11",
        "obs_qsv11_av1",
        "amd_amf_h264",
        "amd_amf_hevc",
        "com.apple.videotoolbox",
        nullptr
    };

    for (int i = 0; supported_encoders[i] != nullptr; i++) {
        if (strcmp(id, supported_encoders[i]) == 0) {
            return true;
        }
    }

    // Unknown encoder - assume it works but log a warning
    blog(LOG_WARNING, "[SRTLA Encoder] Unknown encoder '%s' - runtime bitrate may not work", id);
    return true;
}

std::string EncoderController::getEncoderName() {
    obs_encoder_t *encoder = getStreamingEncoder();
    if (!encoder) {
        return "unknown";
    }

    const char *name = obs_encoder_get_name(encoder);
    if (name) {
        return std::string(name);
    }

    const char *id = obs_encoder_get_id(encoder);
    if (id) {
        return std::string(id);
    }

    return "unknown";
}
