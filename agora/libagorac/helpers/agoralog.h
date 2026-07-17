#ifndef _AGORA_LOG_H_
#define _AGORA_LOG_H_

#include <string>

//enable/disable backend debug logging at runtime (wired to the element's verbose property)
void setLogEnabled(bool enabled);

//log a debug message to /tmp/agora.log; no-op unless logging is enabled
void logMessage(const std::string& message);

//always-on tier for connection lifecycle events (connected/disconnected,
//user joined/left, publish state): printed to stdout so they reach the
//journal regardless of the verbose property, and mirrored to the log file
//when debug logging is enabled. Keep this for low-frequency messages only.
void logInfo(const std::string& message);

#endif
