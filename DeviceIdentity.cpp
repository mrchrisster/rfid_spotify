#include "DeviceIdentity.h"
#include "CertificatePolicy.h"
#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/oid.h>
#include <mbedtls/sha256.h>
#include <time.h>
#include <memory>
#include <new>
#if __has_include("DeviceCertificate.h")
#include "DeviceCertificate.h"
#else
static const char deviceHostname[] = "spotify-player";
static const char deviceCertificate[] = "", devicePrivateKey[] = "";
static const int64_t deviceCertificateExpiresUnix = 0;
#endif
#ifndef DEVICE_AUTO_CERTIFICATE
#define DEVICE_AUTO_CERTIFICATE 0
static const char deviceIssuerCertificate[] = "", deviceIssuerPrivateKey[] = "";
#endif
void logMessage(const String&);
namespace DeviceIdentity {
namespace {
Preferences storage;
String leaf;
char servedChain[4096] = {};
int64_t expires = 0, validFrom = 0, issuerExpires = 0;
uint32_t checkedAt = 0;
bool checked = false, storageOpen = false, persisted = false;
const char* status = "Not provisioned";
int rng(void*, unsigned char* bytes, size_t length) { esp_fill_random(bytes,length); return 0; }
struct Crypto {
  mbedtls_x509_crt cert, issuer;
  mbedtls_pk_context subjectKey, issuerKey;
  mbedtls_x509write_cert writer;
  Crypto() {
    mbedtls_x509_crt_init(&cert); mbedtls_x509_crt_init(&issuer);
    mbedtls_pk_init(&subjectKey); mbedtls_pk_init(&issuerKey); mbedtls_x509write_crt_init(&writer);
  }
  ~Crypto() {
    mbedtls_x509_crt_free(&cert); mbedtls_x509_crt_free(&issuer);
    mbedtls_pk_free(&subjectKey); mbedtls_pk_free(&issuerKey); mbedtls_x509write_crt_free(&writer);
  }
};
int parseKey(mbedtls_pk_context* key, const char* pem) {
  return mbedtls_pk_parse_key(key,reinterpret_cast<const unsigned char*>(pem),strlen(pem)+1,nullptr,0,rng,nullptr);
}
int parseCert(mbedtls_x509_crt* cert, const char* pem) {
  return mbedtls_x509_crt_parse(cert,reinterpret_cast<const unsigned char*>(pem),strlen(pem)+1);
}
int64_t epoch(const mbedtls_x509_time& value) {
  return CertificatePolicy::utcEpoch(value.year,value.mon,value.day,value.hour,value.min,value.sec);
}
bool validate(const char* pem, int64_t& expiry, int64_t* from = nullptr) {
  std::unique_ptr<Crypto> allocated(new(std::nothrow) Crypto);
  if (!allocated) return false;
  Crypto& c=*allocated;
  if (parseCert(&c.cert,pem) || c.cert.next || parseCert(&c.issuer,deviceIssuerCertificate) ||
      parseKey(&c.subjectKey,devicePrivateKey) || mbedtls_pk_check_pair(&c.cert.pk,&c.subjectKey,rng,nullptr)) return false;
  // Verify against the pinned issuer, not a general trust store; permit expired
  // dates here so a device returning after a long outage can load then renew.
  uint32_t flags=0;
  int verified=mbedtls_x509_crt_verify(&c.cert,&c.issuer,nullptr,(String(deviceHostname)+".local").c_str(),&flags,nullptr,nullptr);
  constexpr uint32_t dates=MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE;
  if (verified && (!flags || (flags & ~dates))) return false;
  if (mbedtls_x509_crt_get_ca_istrue(&c.cert) || MBEDTLS_OID_CMP(MBEDTLS_OID_ECDSA_SHA256,&c.cert.sig_oid)) return false;
  expiry=epoch(c.cert.valid_to);
  int64_t start=epoch(c.cert.valid_from);
  if (from) *from=start;
  return expiry > start && expiry-start <= 398 * CertificatePolicy::Day && expiry <= epoch(c.issuer.valid_to);
}
void updateChain() {
  size_t leafSize=leaf.length(), issuerSize=DEVICE_AUTO_CERTIFICATE ? strlen(deviceIssuerCertificate) : 0;
  if (leafSize+issuerSize>=sizeof(servedChain)) { status="Certificate chain too large"; return; }
  memcpy(servedChain,leaf.c_str(),leafSize);
  if (issuerSize) memcpy(servedChain+leafSize,deviceIssuerCertificate,issuerSize);
  servedChain[leafSize+issuerSize]=0;
}
bool generate(int64_t now, String& candidate, int64_t& expiry) {
  std::unique_ptr<Crypto> allocated(new(std::nothrow) Crypto);
  if (!allocated) return false;
  Crypto& c=*allocated;
  if (parseCert(&c.issuer,deviceIssuerCertificate) || parseKey(&c.subjectKey,devicePrivateKey) ||
      parseKey(&c.issuerKey,deviceIssuerPrivateKey) || mbedtls_pk_check_pair(&c.issuer.pk,&c.issuerKey,rng,nullptr)) return false;
  int64_t until=CertificatePolicy::nextExpiry(now,epoch(c.issuer.valid_to));
  if (until-now <= CertificatePolicy::RenewBefore || now < epoch(c.issuer.valid_from)) return false;
  char issuerName[256], subjectName[96], from[16], to[16];
  if (mbedtls_x509_dn_gets(issuerName,sizeof(issuerName),&c.issuer.subject)<0) return false;
  snprintf(subjectName,sizeof(subjectName),"CN=%s.local",deviceHostname);
  time_t first=now-300, last=until; tm t{};
  gmtime_r(&first,&t); strftime(from,sizeof(from),"%Y%m%d%H%M%S",&t);
  gmtime_r(&last,&t); strftime(to,sizeof(to),"%Y%m%d%H%M%S",&t);
  mbedtls_x509write_crt_set_version(&c.writer,MBEDTLS_X509_CRT_VERSION_3);
  mbedtls_x509write_crt_set_md_alg(&c.writer,MBEDTLS_MD_SHA256);
  mbedtls_x509write_crt_set_subject_key(&c.writer,&c.subjectKey);
  mbedtls_x509write_crt_set_issuer_key(&c.writer,&c.issuerKey);
  unsigned char serial[16]; rng(nullptr,serial,sizeof(serial)); serial[0]=(serial[0]&0x7f)|1;
  if (mbedtls_x509write_crt_set_serial_raw(&c.writer,serial,sizeof(serial)) ||
      mbedtls_x509write_crt_set_subject_name(&c.writer,subjectName) ||
      mbedtls_x509write_crt_set_issuer_name(&c.writer,issuerName) ||
      mbedtls_x509write_crt_set_validity(&c.writer,from,to) ||
      mbedtls_x509write_crt_set_basic_constraints(&c.writer,0,-1) ||
      mbedtls_x509write_crt_set_key_usage(&c.writer,MBEDTLS_X509_KU_DIGITAL_SIGNATURE)) return false;
  // DER SEQUENCE { [2] DNSName }; a validated DNS label + '.local' fits short lengths.
  String dns=String(deviceHostname)+".local"; unsigned char san[80];
  san[0]=0x30; san[1]=dns.length()+2; san[2]=0x82; san[3]=dns.length(); memcpy(san+4,dns.c_str(),dns.length());
  const unsigned char eku[]={0x30,0x0a,0x06,0x08,0x2b,0x06,0x01,0x05,0x05,0x07,0x03,0x01};
  if (mbedtls_x509write_crt_set_extension(&c.writer,MBEDTLS_OID_SUBJECT_ALT_NAME,MBEDTLS_OID_SIZE(MBEDTLS_OID_SUBJECT_ALT_NAME),0,san,dns.length()+4) ||
      mbedtls_x509write_crt_set_extension(&c.writer,MBEDTLS_OID_EXTENDED_KEY_USAGE,MBEDTLS_OID_SIZE(MBEDTLS_OID_EXTENDED_KEY_USAGE),0,eku,sizeof(eku))) return false;
  unsigned char pem[2048];
  if (mbedtls_x509write_crt_pem(&c.writer,pem,sizeof(pem),rng,nullptr)) return false;
  candidate=reinterpret_cast<char*>(pem);
  return validate(candidate.c_str(),expiry) && expiry==until;
}
struct Store {
  bool write(const String& value) { return storage.putString("leaf",value)==value.length(); }
  String read() { return storage.getString("leaf",""); }
};
int days(int64_t expiry) {
  int64_t now=time(nullptr);
  if (!expiry || !CertificatePolicy::timeReady(now)) return -1;
  return expiry<=now ? 0 : int((expiry-now)/CertificatePolicy::Day);
}
}
void begin() {
  leaf=deviceCertificate; expires=deviceCertificateExpiresUnix;
  if (DEVICE_AUTO_CERTIFICATE) {
    std::unique_ptr<Crypto> allocated(new(std::nothrow) Crypto);
    if (!allocated) { status="Low memory; restart required"; updateChain(); return; }
    Crypto& c=*allocated;
    if (!parseCert(&c.issuer,deviceIssuerCertificate)) issuerExpires=epoch(c.issuer.valid_to);
    int64_t bootstrapExpiry=0;
    validate(leaf.c_str(),bootstrapExpiry,&validFrom);
    storageOpen=storage.begin("tls_identity",false);
    String saved=storageOpen ? storage.getString("leaf","") : String();
    int64_t savedExpiry=0;
    if (!saved.isEmpty() && saved.length()<2048 && validate(saved.c_str(),savedExpiry) && savedExpiry>=expires) {
      leaf=saved; expires=savedExpiry; persisted=true;
      validate(leaf.c_str(),savedExpiry,&validFrom);
    }
    status=storageOpen ? "Automatic" : "Storage unavailable; retrying";
  } else if (deviceCertificate[0]) status="Manual certificate";
  updateChain();
}
bool maintain() {
  int64_t now=time(nullptr);
  if (!DEVICE_AUTO_CERTIFICATE || !CertificatePolicy::timeReady(now)) return false;
  if (checked && uint32_t(millis()-checkedAt)<3600000) return false;
  checked=true; checkedAt=millis();
  if (persisted && now>=validFrom && !CertificatePolicy::due(now,expires)) { status="Automatic"; return false; }
  if (CertificatePolicy::nextExpiry(now,issuerExpires)-now<=CertificatePolicy::RenewBefore) {
    status="Signing authority nearing expiry; reprovision required"; return false;
  }
  if (!storageOpen) storageOpen=storage.begin("tls_identity",false);
  if (!storageOpen) { status="Storage unavailable; retrying"; return false; }
  String candidate; int64_t replacementExpiry=0;
  if (!generate(now,candidate,replacementExpiry)) {
    status="Renewal failed; retrying hourly"; logMessage("[HTTPS] Certificate renewal failed; keeping current certificate"); return false;
  }
  Store store;
  if (!CertificatePolicy::commit(store,candidate,leaf)) {
    status="Certificate save failed; retrying hourly"; logMessage("[HTTPS] Certificate save failed; keeping current certificate"); return false;
  }
  expires=replacementExpiry; validFrom=now-300; persisted=true; status="Automatic; certificate renewed";
  logMessage("[HTTPS] Certificate renewed and saved"); return true;
}
void activate() { updateChain(); }
const char* hostname() { return deviceHostname; }
const char* key() { return devicePrivateKey; }
const char* chain() { return servedChain; }
bool automatic() { return DEVICE_AUTO_CERTIFICATE; }
int certificateDays() { return days(expires); }
int issuerDays() { return days(issuerExpires); }
const char* renewalStatus() { return status; }
}
