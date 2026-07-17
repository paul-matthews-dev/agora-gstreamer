#ifndef _AGORA_LOG_H_
#define _AGORA_LOG_H_

#include <string>

//enable/disable backend logging at runtime (wired to the element's verbose property)
void setLogEnabled(bool enabled);

//log a debug message to /tmp/agora.log; no-op unless logging is enabled
void logMessage(const std::string& message);

#endif
