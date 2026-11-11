/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */
#include "stereo-jpeg.h"
#include <string.h>
static uint16_t be16(const unsigned char *p) { return (uint16_t)((unsigned)p[0]*256+p[1]); }
static void put16(unsigned char *p,uint16_t v) { p[0]=(unsigned char)(v>>8); p[1]=(unsigned char)v; }
static void put32(unsigned char *p,uint32_t v) { put16(p,(uint16_t)(v>>16)); put16(p+2,(uint16_t)v); }
static int jps_valid(uint32_t v) {
    unsigned type=v&255, layout=(v>>8)&255, flags=(v>>16)&255, gap=v>>24;
    if(flags&~7u) return 0;
    if(type==0) return layout<=2 && !gap && !(flags&4);
    return type==1 && layout>=1 && layout<=4 && ((layout==2 || layout==3) || !gap);
}
int stereo_jps_decode(const void *payload,size_t size,struct stereo_jps_descriptor *out) {
    const unsigned char *p=payload;
    if(!p || !out || size<16 || size>65533 || memcmp(p,"_JPSJPS_",8)) return 0;
    size_t length=be16(p+8);
    if(length<4 || length>size-12) return 0;
    size_t comments=be16(p+10+length), end=12+length+comments;
    if(end!=size && !(end+1==size && p[end]==0)) return 0;
    uint32_t v=(uint32_t)be16(p+10)<<16 | be16(p+12);
    if(!jps_valid(v)) return 0;
    for(size_t i=0;i<comments;++i) if(p[12+length+i]>127) return 0;
    *out=(struct stereo_jps_descriptor){v,p+14,p+12+length,(uint16_t)(length-4),(uint16_t)comments};
    return 1;
}
size_t stereo_jps_encode(const struct stereo_jps_descriptor *d,void *output,size_t capacity) {
    if(!d || !jps_valid(d->value) || (d->extension_size && !d->extension) || (d->comment_size && !d->comment)) return 0;
    size_t n=16u+d->extension_size+d->comment_size;
    if(n>65533 || !output || n>capacity) return 0;
    for(size_t i=0;i<d->comment_size;++i) if(d->comment[i]>127) return 0;
    unsigned char *p=output;
    memcpy(p,"_JPSJPS_",8); put16(p+8,(uint16_t)(4+d->extension_size)); put32(p+10,d->value);
    if(d->extension_size) memcpy(p+14,d->extension,d->extension_size);
    put16(p+14+d->extension_size,d->comment_size);
    if(d->comment_size) memcpy(p+16+d->extension_size,d->comment,d->comment_size);
    return n;
}
static int tiff_header(const unsigned char *p,size_t n) {
    return p && n>=8 && ((p[0]=='I' && p[1]=='I' && p[2]==42 && p[3]==0) || (p[0]=='M' && p[1]=='M' && p[2]==0 && p[3]==42));
}
static uint16_t u16(const unsigned char *p,size_t n,size_t o) {
    if(o>n || n-o<2) return 0;
    return p[0]=='I' ? (uint16_t)(p[o]+256u*p[o+1]) : be16(p+o);
}
uint32_t stereo_mpf_u32(const void *data,size_t n,size_t o) {
    const unsigned char *p=data;
    if(!tiff_header(p,n) || o>n || n-o<4) return 0;
    return p[0]=='I' ? (uint32_t)u16(p,n,o) | (uint32_t)u16(p,n,o+2)<<16 : (uint32_t)u16(p,n,o)<<16 | u16(p,n,o+2);
}
static int ifd_valid(const unsigned char *p,size_t n,uint32_t ifd) {
    return ifd>=8 && ifd<=n && n-ifd>=6 && (size_t)u16(p,n,ifd)<=(n-ifd-6)/12;
}
uint32_t stereo_mpf_next(const void *data,size_t n,uint32_t ifd) {
    const unsigned char *p=data;
    if(!tiff_header(p,n) || !ifd_valid(p,n,ifd)) return 0;
    return stereo_mpf_u32(p,n,ifd+2u+12u*u16(p,n,ifd));
}
size_t stereo_mpf_field(const void *data,size_t n,uint32_t ifd,uint16_t tag,uint16_t type,uint32_t count) {
    const unsigned char *p=data;
    if(!tiff_header(p,n) || !ifd_valid(p,n,ifd)) return 0;
    size_t found=0;
    for(size_t i=0;i<u16(p,n,ifd);++i) {
        size_t o=ifd+2+i*12;
        if(u16(p,n,o)==tag) {
            if(found || u16(p,n,o+2)!=type || stereo_mpf_u32(p,n,o+4)!=count) return 0;
            found=o+8;
        }
    }
    return found;
}
int stereo_mpf_valid(const void *data,size_t n) {
    const unsigned char *p=data;
    if(!tiff_header(p,n)) return 0;
    uint32_t ifd=stereo_mpf_u32(p,n,4);
    /* A chain cannot have more IFDs than disjoint six-byte empty directories. */
    size_t remaining=n/6;
    while(ifd) {
        if(!remaining-- || !ifd_valid(p,n,ifd)) return 0;
        for(size_t i=0;i<u16(p,n,ifd);++i) {
            size_t o=ifd+2+12*i;
            uint16_t type=u16(p,n,o+2);
            static const unsigned sizes[]={0,1,1,2,4,8,1,1,2,4,8,4,8};
            if(type<1 || type>12) return 0;
            uint64_t bytes=(uint64_t)sizes[type]*stereo_mpf_u32(p,n,o+4);
            uint32_t offset=stereo_mpf_u32(p,n,o+8);
            if(bytes>4 && (offset<8 || offset>n || bytes>n-offset)) return 0;
            for(size_t j=0;j<i;++j) if(u16(p,n,ifd+2+12*j)==u16(p,n,o)) return 0;
        }
        ifd=stereo_mpf_next(p,n,ifd);
    }
    return stereo_mpf_u32(p,n,4)!=0;
}
int stereo_mpf_set_u32(void *data,size_t n,size_t o,uint32_t v) {
    unsigned char *p=data;
    if(!tiff_header(p,n) || o>n || n-o<4) return 0;
    if(p[0]=='M') put32(p+o,v);
    else for(unsigned i=0;i<4;++i) p[o+i]=(unsigned char)(v>>(8*i));
    return 1;
}
static void le16(unsigned char *p,uint16_t v) { p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8); }
static void le32(unsigned char *p,uint32_t v) { for(unsigned i=0;i<4;++i) p[i]=(unsigned char)(v>>(8*i)); }
static void field(unsigned char *p,uint16_t tag,uint16_t type,uint32_t count,uint32_t value) {
    le16(p,tag); le16(p+2,type); le32(p+4,count); le32(p+8,value);
}
size_t stereo_mpf_encode_pair(const struct stereo_mpf_entry *entries,void *output,size_t capacity) {
    size_t size=entries?124:50;
    if(!output || capacity<size) return 0;
    unsigned char *p=output; memset(p,0,size); memcpy(p,"II",2); le16(p+2,42); le32(p+4,8);
    size_t attr=8;
    if(entries) {
        le16(p+8,3); field(p+10,0xb000,7,4,0x30303130); field(p+22,0xb001,4,1,2); field(p+34,0xb002,7,32,92); le32(p+46,50); attr=50;
        for(unsigned i=0;i<2;++i) { size_t o=92+16*i; le32(p+o,entries[i].attributes); le32(p+o+4,entries[i].size); le32(p+o+8,entries[i].offset); }
    }
    le16(p+attr,3); field(p+attr+2,0xb000,7,4,0x30303130); field(p+attr+14,0xb101,4,1,entries?1:2); field(p+attr+26,0xb204,4,1,1);
    return size;
}
