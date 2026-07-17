#ifndef _CONTEXT_H_
#define _CONTEXT_H_

#include <cstdint>
#include <memory>
#include <vector>

class AgoraIo;
struct AgoraIoContext_t{

   std::shared_ptr<AgoraIo>  agoraIo;
};

//one buffered A/V frame travelling through a WorkQueue/SyncBuffer
class Work{

public:

   Work(const unsigned char* b, size_t l, bool is_key):
        buffer(b, b+l),
        is_key_frame(is_key),
        timestamp(0){}

   std::vector<unsigned char>   buffer;
   bool                         is_key_frame;
   uint64_t                     timestamp;
};

#endif
