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

#ifndef OBS_SRTLA_ENCODER_CONTROLLER_H
#define OBS_SRTLA_ENCODER_CONTROLLER_H

#include <obs-module.h>
#include <string>

/*
 * EncoderController - Controls OBS encoder bitrate dynamically
 *
 * This class provides methods to update the video encoder's bitrate
 * setting at runtime, with proper handling of different encoder types.
 */
class EncoderController {
public:
    EncoderController();
    ~EncoderController();

    // Set encoder bitrate (Kbps)
    // Returns true if successful
    bool setBitrate(int bitrate_kbps);

    // Get current encoder bitrate (Kbps)
    // Returns -1 if unable to get
    int getBitrate();

    // Check if encoder supports runtime bitrate changes
    bool supportsRuntimeBitrate();

    // Get encoder ID/name for logging
    std::string getEncoderName();

    // Get last error message
    std::string getLastError() const { return m_lastError; }

private:
    // Get streaming video encoder
    obs_encoder_t* getStreamingEncoder();

    // Detect the bitrate property name for this encoder
    std::string detectBitrateProperty(obs_encoder_t *encoder);

    int m_lastBitrate;
    std::string m_bitrateProperty;  // Cached property name
    std::string m_lastError;
};

#endif /* OBS_SRTLA_ENCODER_CONTROLLER_H */
