#include <assert.h>
#include "../devices/umi/package/touch/UmiFtsCore.h"
static void event(uint8_t *e,unsigned op,unsigned id,unsigned x,unsigned y,unsigned type=1) {
 memset(e,0,8);e[0]=op;e[1]=(id<<4)|type;e[2]=x;e[3]=(x>>8)|((y&15)<<4);e[4]=y>>4;
}
int main() {
 UmiTouchFrame f={},old={};uint8_t e[8],r[62];
 // Native high-nibble dispatch: size bits must never suppress a real contact
 // or prevent its lift. Keep coordinate/ID validation for every encoding.
 for(unsigned low=0;low<16;++low){
  UmiTouchFrame a={};
  event(e,0x10|low,4,530,2200);assert(umi_fts_point_event(e[0]));
  assert(umi_fts_parse(&a,e)==UMI_FTS_POINT && a.points[4].active);
  event(e,0x20|low,4,532,1800);assert(umi_fts_parse(&a,e)==UMI_FTS_POINT);
  assert(a.points[4].x==532 && a.points[4].y==1800);
  event(e,0x30|low,4,4095,4095);assert(umi_fts_parse(&a,e)==UMI_FTS_POINT && !a.points[4].active);
  event(e,0x10|low,10,530,2200);assert(umi_fts_parse(&a,e)==UMI_FTS_BAD);
  event(e,0x20|low,4,1080,2340);assert(umi_fts_parse(&a,e)==UMI_FTS_BAD);
 }
 for(unsigned kind=4;kind<15;++kind){
  event(e,(kind<<4)|3,0,100,100);assert(!umi_fts_point_event(e[0]));
  assert(umi_fts_parse(&f,e)==UMI_FTS_NONE);
 }
 event(e,0x13,0,1079,2339);assert(umi_fts_parse(&f,e)==UMI_FTS_POINT);
 assert(umi_fts_report(&old,&f,r)==1 && r[1]==3 && r[3]==255 && r[4]==127 && r[5]==255 && r[6]==127);
 old=f;event(e,0x13,1,100,200);assert(umi_fts_parse(&f,e)==UMI_FTS_POINT);
 assert(umi_fts_report(&old,&f,r)==2 && r[1]==3 && r[7]==3 && r[8]==1);
 old=f;event(e,0x33,0,4095,4095);assert(umi_fts_parse(&f,e)==UMI_FTS_POINT);
 assert(umi_fts_report(&old,&f,r)==2 && r[1]==2 && r[2]==0 && r[3]==255 && r[7]==3 && r[8]==1);
 old=f;event(e,0x23,1,101,201);assert(umi_fts_parse(&f,e)==UMI_FTS_POINT);
 assert(umi_fts_report(&old,&f,r)==1 && r[1]==0 && r[7]==3);
 old=f;event(e,0x23,1,101,201,5);assert(umi_fts_parse(&f,e)==UMI_FTS_POINT);
 assert(umi_fts_report(&old,&f,r)==1 && r[7]==2);
 event(e,0x13,9,1080,0);assert(umi_fts_parse(&f,e)==UMI_FTS_BAD);
 event(e,0x13,10,0,0);assert(umi_fts_parse(&f,e)==UMI_FTS_BAD);
 for(unsigned i=0;i<10;++i){event(e,0x13,i,i*100,i*200);assert(umi_fts_parse(&f,e)==UMI_FTS_POINT);}
 old=f;memset(e,0,8);e[0]=3;assert(umi_fts_parse(&f,e)==UMI_FTS_RESET);
 assert(umi_fts_report(&old,&f,r)==10);for(unsigned i=0;i<10;++i)assert(r[1+i*6]==2 && r[2+i*6]==i);
 // Exhaust all event kinds/IDs and full 12-bit wire coordinate space, including invalid bounds.
 for(unsigned x=0;x<4096;++x){event(e,0x13,0,x,0);assert(umi_fts_parse(&f,e)==(x<1080?UMI_FTS_POINT:UMI_FTS_BAD));}
 for(unsigned y=1;y<4096;++y){event(e,0x13,0,0,y);assert(umi_fts_parse(&f,e)==(y<2340?UMI_FTS_POINT:UMI_FTS_BAD));}
}
