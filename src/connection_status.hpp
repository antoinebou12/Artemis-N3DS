#pragma once

#include <Limelight.h>

#include <string>

inline std::string connection_termination_user_message(int errorCode) {
    switch (errorCode) {
    case ML_ERROR_GRACEFUL_TERMINATION:
        return "";
    case ML_ERROR_NO_VIDEO_TRAFFIC:
        return "No video from the host. Check firewall and port "
               "forwarding on the PC.";
    case ML_ERROR_NO_VIDEO_FRAME:
        return "Network is too slow for this bitrate. Lower video "
               "quality or move closer to Wi-Fi.";
    case ML_ERROR_UNEXPECTED_EARLY_TERMINATION:
        return "Host stopped streaming. Close DRM-protected apps on "
               "the PC and try again.";
    case ML_ERROR_PROTECTED_CONTENT:
        return "Host blocked the stream because DRM content is playing "
               "on the PC.";
    case ML_ERROR_FRAME_CONVERSION:
        return "Video decode failed on this 3DS. Try a lower "
               "resolution or software decoding.";
    default:
        return "Connection ended with error " + std::to_string(errorCode) +
               ".";
    }
}
