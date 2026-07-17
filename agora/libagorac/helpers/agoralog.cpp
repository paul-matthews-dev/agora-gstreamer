#include "agoralog.h"

#include <atomic>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sys/time.h>

static std::atomic<bool> g_logEnabled{false};
static std::mutex g_logMutex;

static constexpr std::streamoff MAX_LOG_FILE_SIZE = 250LL * 1024 * 1024;
static const char* LOG_FILE_NAME = "/tmp/agora.log";

void setLogEnabled(bool enabled){
  g_logEnabled.store(enabled, std::memory_order_relaxed);
}

void logMessage(const std::string& message){

  if(!g_logEnabled.load(std::memory_order_relaxed)){
     return;
  }

  char buffer[30];
  struct timeval tv;
  gettimeofday(&tv, NULL);
  time_t curtime=tv.tv_sec;
  strftime(buffer, sizeof(buffer), "%m-%d-%Y  %T.", localtime(&curtime));
  int ms=(int)((tv.tv_usec)/1000);

  char fullTime[40];
  snprintf(fullTime, sizeof(fullTime), "%s%3d", buffer, ms);

  std::lock_guard<std::mutex> guard(g_logMutex);

  // keep the stream open between messages; reopen (truncating) if it grew
  // past the cap so a long-running session cannot fill /tmp.
  static std::ofstream file(LOG_FILE_NAME, std::ios::app);
  if(file.is_open() && file.tellp() > MAX_LOG_FILE_SIZE){
     file.close();
     file.open(LOG_FILE_NAME, std::ios::trunc);
  }
  if(!file.is_open()){
     return;
  }
  file<<fullTime<<": "<<message<<'\n';
  file.flush();
}

void logInfo(const std::string& message){

  //stdout is captured by the supervising process (journal) — always emit
  printf("AgoraIO: %s\n", message.c_str());
  fflush(stdout);

  logMessage(message);
}
