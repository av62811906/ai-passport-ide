#include "restore_journal.h"
#include <string.h>
#include <stddef.h>
#define NEW_AT 0x1000u
#define OLD_AT 0x7000u
#define MAGIC 0x31525750u
struct header { uint32_t magic,size,new_crc,old_crc;uint8_t build[32];uint32_t crc; };
static uint32_t update(uint32_t c,const uint8_t *p,size_t n) {
 while(n--){c^=*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^(0xedb88320u&(0u-(c&1)));}return c;
}
uint32_t restore_crc32(const uint8_t *p,size_t n){return ~update(~0u,p,n);}
static bool digest(const restore_io_t *io,bool stage,uint32_t at,uint32_t *crc) {
 uint8_t buf[256];uint32_t c=~0u;
 for(unsigned i=0;i<RESTORE_IMAGE_SIZE;i+=sizeof(buf)){
  if(!io->read(stage,at+i,buf,sizeof(buf)))return false;
  c=update(c,buf,sizeof(buf));
 }*crc=~c;return true;
}
bool restore_journal_prepare(const restore_io_t *io,const uint8_t *image,const uint8_t build[32]) {
 struct header h={.magic=MAGIC,.size=RESTORE_IMAGE_SIZE};uint8_t buf[256];uint32_t crc;
 if(!io||!image||!build||!io->erase(true,0,RESTORE_STAGE_SIZE))return false;
 h.new_crc=restore_crc32(image,RESTORE_IMAGE_SIZE);memcpy(h.build,build,32);
 if(!io->write(true,NEW_AT,image,RESTORE_IMAGE_SIZE))return false;
 uint32_t old=~0u;
 for(unsigned i=0;i<RESTORE_IMAGE_SIZE;i+=sizeof(buf)){
  if(!io->read(false,i,buf,sizeof(buf))||!io->write(true,OLD_AT+i,buf,sizeof(buf)))return false;
  old=update(old,buf,sizeof(buf));
 }h.old_crc=~old;
 if(!digest(io,true,NEW_AT,&crc)||crc!=h.new_crc||!digest(io,true,OLD_AT,&crc)||crc!=h.old_crc)return false;
 h.crc=restore_crc32((const uint8_t*)&h,offsetof(struct header,crc));
 // Commit marker last: interruption before this point never touches NVS.
 struct header check;
 bool written=io->write(true,0,&h,sizeof(h));
 if(written&&io->read(true,0,&check,sizeof(check))&&!memcmp(&h,&check,sizeof(h)))return true;
 // A failed marker write/read can be ambiguous. Clear it before allowing play;
 // if clearing also fails, require a reboot to resolve it before game mutations.
 return !io->erase(true,0,0x1000);
}
static bool install(const restore_io_t *io,uint32_t at,uint32_t expected) {
 uint8_t buf[256];uint32_t crc;
 if(!io->erase(false,0,RESTORE_IMAGE_SIZE))return false;
 for(unsigned i=0;i<RESTORE_IMAGE_SIZE;i+=sizeof(buf))
  if(!io->read(true,at+i,buf,sizeof(buf))||!io->write(false,i,buf,sizeof(buf)))return false;
 return digest(io,false,0,&crc)&&crc==expected;
}
int restore_journal_apply(const restore_io_t *io,const uint8_t build[32],uint32_t *crc) {
 struct header h;uint32_t c;
 if(!io->read(true,0,&h,sizeof(h)))return -1;
 if(h.magic!=MAGIC||h.size!=RESTORE_IMAGE_SIZE||h.crc!=restore_crc32((const uint8_t*)&h,offsetof(struct header,crc)))return 0;
 if(memcmp(h.build,build,32))return -1;
 bool fresh=digest(io,true,NEW_AT,&c)&&c==h.new_crc;
 bool old=digest(io,true,OLD_AT,&c)&&c==h.old_crc;
 int result=0;
 if(fresh&&install(io,NEW_AT,h.new_crc))result=1;
 else if(old&&install(io,OLD_AT,h.old_crc))result=2;
 if(!result)return -1;
 // No game mutation is possible until this marker is cleared. A reset before
 // clearing retries the exact same image; a reset afterwards boots a verified NVS.
 if(!io->erase(true,0,0x1000))return -1;
 if(crc)*crc=h.new_crc;
 return result;
}
