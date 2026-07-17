#include "utilities.h"
#include "AgoraBase.h"

#include "agoralog.h"

#include <fstream>


TimePoint Now(){
   return std::chrono::steady_clock::now();
}

long GetTimeDiff(const TimePoint& start, const TimePoint& end){
  return std::chrono::duration_cast<std::chrono::milliseconds>(end-start).count();
}

/* The certificate-based license check only exists for SDK build 110077; every
   other build (including the 4.4.x builds this fork targets) needs no
   certificate and the check is a no-op. */
#if SDK_BUILD_NUM==110077

class LicenseCallbackImpl : public agora::base::LicenseCallback
{
public:
  LicenseCallbackImpl() {}
  virtual ~LicenseCallbackImpl() {}

  virtual void onCertificateRequired() {}
  virtual void onLicenseRequest() {}
  virtual void onLicenseValidated() {}
  virtual void onLicenseError(int result) {}
};

static const std::string CERTIFICATE_FILE = "certificate.bin";

int verifyLicense()
{
  std::ifstream f_cert(CERTIFICATE_FILE.c_str(), std::ios::binary);
  if (!f_cert) {
    logMessage(CERTIFICATE_FILE+" doesn't exist");
    return -1;
  }

  std::string cert((std::istreambuf_iterator<char>(f_cert)),
                    std::istreambuf_iterator<char>());
  f_cert.close();

  LicenseCallbackImpl *cb = static_cast<LicenseCallbackImpl *>(getAgoraLicenseCallback());
  if (!cb) {
    cb = new LicenseCallbackImpl();
    setAgoraLicenseCallback(static_cast<agora::base::LicenseCallback *>(cb));
  }

  int result = getAgoraCertificateVerifyResult(NULL, 0, cert.c_str(), (int)cert.size());
  logMessage("license verify result: "+std::to_string(result));

  return result;
}

#else

int verifyLicense(){
	return 0;
}

#endif
