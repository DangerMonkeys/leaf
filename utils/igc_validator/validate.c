/* IgcLogger XLV MD5 checksum checker; not recorder authentication. */
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
static HCRYPTPROV provider;
static HCRYPTHASH hash;
static int init(void) { return CryptAcquireContext(&provider,0,0,PROV_RSA_FULL,CRYPT_VERIFYCONTEXT) && CryptCreateHash(provider,CALG_MD5,0,0,&hash); }
static int add(const unsigned char *p, size_t n) { return CryptHashData(hash,p,(DWORD)n,0); }
static int end(unsigned char *p) { DWORD n=16; return CryptGetHashParam(hash,HP_HASHVAL,p,&n,0); }
static void cleanup(void) { if(hash) CryptDestroyHash(hash); if(provider) CryptReleaseContext(provider,0); }
#elif defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
static CC_MD5_CTX hash;
static int init(void) { return CC_MD5_Init(&hash); }
static int add(const unsigned char *p, size_t n) { return CC_MD5_Update(&hash,p,(CC_LONG)n); }
static int end(unsigned char *p) { return CC_MD5_Final(p,&hash); }
static void cleanup(void) {}
#else
#include <openssl/evp.h>
static EVP_MD_CTX *hash;
static int init(void) { hash=EVP_MD_CTX_new(); return hash && EVP_DigestInit_ex(hash,EVP_md5(),0); }
static int add(const unsigned char *p, size_t n) { return EVP_DigestUpdate(hash,p,n); }
static int end(unsigned char *p) { return EVP_DigestFinal_ex(hash,p,0); }
static void cleanup(void) { EVP_MD_CTX_free(hash); }
#endif

int main(int argc, char **argv) {
  unsigned char line[4096], digest[16];
  char expected[33]={0}, actual[33];
  const char *error=0;
  FILE *file=0;
  size_t records=0, fixes=0;
  int result=1, c;
  if(argc!=2) { fprintf(stderr,"Usage: %s flight.igc\n",argv[0]); return 2; }
  file=fopen(argv[1],"rb");
  if(!file) { error="cannot open file"; result=2; goto done; }
  if(!init()) { error="MD5 initialization failed"; result=2; goto done; }
  while((c=fgetc(file))!=EOF) {
    size_t n=0;
    do {
      if(c=='\n') break;
      if(n==sizeof(line)) { error="record too long"; goto done; }
      line[n++]=(unsigned char)c;
    } while((c=fgetc(file))!=EOF);
    if(n && line[n-1]=='\r') --n;
    if(!n) { error="empty record"; goto done; }
    for(size_t i=0;i<n;++i)
      if(line[i]<32 || line[i]>126) { error="non-printable record"; goto done; }
    if(!records && (n<7 || memcmp(line,"AXLV",4))) { error="expected AXLV header"; goto done; }
    if(expected[0]) { error="record after G"; goto done; }
    if(line[0]=='G') {
      if(n!=33) { error="invalid G length"; goto done; }
      for(size_t i=1;i<n;++i)
        if(!strchr("0123456789ABCDEF",line[i])) { error="invalid G hex"; goto done; }
      memcpy(expected,line+1,32);
    } else {
      if(records && line[0]=='A') { error="duplicate A"; goto done; }
      if(!add(line,n) || !add((const unsigned char *)"\n",1)) { error="MD5 update failed"; result=2; goto done; }
      fixes+=line[0]=='B';
    }
    ++records;
  }
  if(ferror(file)) { error="read failed"; result=2; goto done; }
  if(!expected[0]) { error="missing G"; goto done; }
  if(!fixes) { error="no B records"; goto done; }
  if(!end(digest)) { error="MD5 finalization failed"; result=2; goto done; }
  for(size_t i=0;i<16;++i) sprintf(actual+2*i,"%02X",digest[i]);
  if(strcmp(actual,expected)) { error="G checksum mismatch"; goto done; }
  result=0;
done:
  if(file) fclose(file);
  cleanup();
  puts(result==0 ? "STATUS_IGC_PASSED" : result==1 ? "STATUS_IGC_FAILED" : "STATUS_IGC_ERROR");
  if(error) fprintf(stderr,"%s\n",error);
  return result;
}
