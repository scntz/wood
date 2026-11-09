/* Hosted test harness only. Shipping installer adapters remain freestanding. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static DWORD WINAPI release_reader(void *value){Sleep(20);CloseHandle((HANDLE)value);return 0;}
#endif
#include "../native/sha256.h"
extern int64_t wood_setup_open(const unsigned char*,int64_t,int64_t);
extern int64_t wood_setup_close(void);
extern int64_t wood_setup_nonce(int64_t*,int64_t);
extern int64_t wood_setup_kind(const unsigned char*,int64_t);
extern int64_t wood_setup_write(const unsigned char*,int64_t,const unsigned char*,int64_t,int64_t);
extern int64_t wood_setup_hash(const unsigned char*,int64_t,int64_t,int64_t,int64_t*,int64_t);
extern int64_t wood_setup_extract(const unsigned char*,int64_t,int64_t,int64_t,const unsigned char*,int64_t,const unsigned char*,int64_t);
extern int64_t wood_setup_remove(const unsigned char*,int64_t,int64_t);
extern void wood_setup_poll_function(int(*callback)(void));
static int cancel_poll(void){return -1;}
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char **argv) {
    CHECK(argc==2);
    const char *expected[]={"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"};
    for(unsigned i=0;i<3;++i){SetupHash h;unsigned char out[64];hash_init(&h);if(i==1)hash_add(&h,(const unsigned char*)"abc",3);if(i==2)for(unsigned n=0;n<1000000;++n)hash_add(&h,(const unsigned char*)"a",1);hash_end(&h,out);CHECK(memcmp(out,expected[i],64)==0);}
    int64_t token[64];CHECK(wood_setup_nonce(token,64)==0);
    char root[4096],source[4200];
    int n=snprintf(root,sizeof(root),"%s/run-",argv[1]);CHECK(n>0&&n<(int)sizeof(root)-65);
    for(unsigned i=0;i<32;++i)root[n++]=(char)token[i];root[n]=0;
    CHECK(snprintf(source,sizeof(source),"%s/owned",root)>0);
    CHECK(wood_setup_open((const unsigned char*)root,(int64_t)strlen(root),1)==0);
    CHECK(wood_setup_write((const unsigned char*)"../bad",6,(const unsigned char*)"x",1,0)<0);
    CHECK(wood_setup_write((const unsigned char*)"owned",5,(const unsigned char*)"abc",3,0)==0);
    CHECK(wood_setup_write((const unsigned char*)"owned",5,(const unsigned char*)"bad",3,0)<0);
    CHECK(wood_setup_hash((const unsigned char*)source,(int64_t)strlen(source),0,-1,token,64)==0);
    for(unsigned i=0;i<64;++i)CHECK(token[i]==expected[1][i]);
    CHECK(wood_setup_extract((const unsigned char*)source,(int64_t)strlen(source),0,3,(const unsigned char*)"owned",5,(const unsigned char*)expected[1],64)<0);
    CHECK(wood_setup_kind((const unsigned char*)"owned",5)==1);
    CHECK(wood_setup_extract((const unsigned char*)source,(int64_t)strlen(source),0,4,(const unsigned char*)"short",5,(const unsigned char*)expected[1],64)<0);
    CHECK(wood_setup_kind((const unsigned char*)"short",5)==0);
    CHECK(wood_setup_extract((const unsigned char*)source,(int64_t)strlen(source),0,3,(const unsigned char*)"copy",4,(const unsigned char*)expected[1],64)==0);
    CHECK(wood_setup_remove((const unsigned char*)"copy",4,0)==0);
    wood_setup_poll_function(cancel_poll);
    CHECK(wood_setup_hash((const unsigned char*)source,(int64_t)strlen(source),0,-1,token,64)==-125);
    /* Cancellation disarms the callback so recovery can hash/remove safely. */
    CHECK(wood_setup_hash((const unsigned char*)source,(int64_t)strlen(source),0,-1,token,64)==0);
    wood_setup_poll_function(cancel_poll);
    CHECK(wood_setup_extract((const unsigned char*)source,(int64_t)strlen(source),0,3,(const unsigned char*)"cancelled",9,(const unsigned char*)expected[1],64)==-125);
    CHECK(wood_setup_kind((const unsigned char*)"cancelled",9)==0);
#ifdef _WIN32
    WCHAR wide_source[32768];
    CHECK(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,source,-1,wide_source,32768)>0);
    HANDLE reader=CreateFileW(wide_source,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);
    CHECK(reader!=INVALID_HANDLE_VALUE);
    HANDLE worker=CreateThread(0,0,release_reader,reader,0,0);CHECK(worker!=0);
    int64_t published=wood_setup_write((const unsigned char*)"owned",5,(const unsigned char*)"updated",7,1);
    CHECK(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);CloseHandle(worker);
    CHECK(published==0);
    CHECK(SetFileAttributesW(wide_source,FILE_ATTRIBUTE_READONLY));
    CHECK(wood_setup_write((const unsigned char*)"owned",5,(const unsigned char*)"must not replace",16,1)<0);
    CHECK(SetFileAttributesW(wide_source,FILE_ATTRIBUTE_NORMAL));
    CHECK(wood_setup_hash((const unsigned char*)source,(int64_t)strlen(source),0,-1,token,64)==0);
    SetupHash wanted;unsigned char wanted_hex[64];hash_init(&wanted);hash_add(&wanted,(const unsigned char*)"updated",7);hash_end(&wanted,wanted_hex);
    for(unsigned i=0;i<64;++i)CHECK(token[i]==wanted_hex[i]);
#endif
    CHECK(wood_setup_close()==0);
    CHECK(wood_setup_kind((const unsigned char*)"owned",5)<0);
    printf("installer native adapter: %u checks passed\n",checks);return 0;
}
