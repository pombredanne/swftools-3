/* q.c

   Part of the swftools package.
   
   Copyright (c) 2001,2002,2003,2004 Matthias Kramm <kramm@quiss.org>
 
   This program is rfx_free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the rfx_free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the rfx_free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <memory.h>
#include "mem.h"
#include "q.h"

// ------------------------------- malloc, alloc routines ---------------------

#ifndef STRNDUP
char* strdup_n(const char*str, int size)
{
    char*m = (char*)rfx_alloc(size+1);
    memcpy(m, str, size);
    m[size] = 0;
    return m;
}
#endif
char*qstrdup(const char*string)
{
    return strdup(string);
}
char*qstrndup(const char*string, int len)
{
    return strdup_n(string, len);
}

// ------------------------------- mem_t --------------------------------------

void mem_init(mem_t*mem)
{
    memset(mem, 0, sizeof(mem_t));
}
void mem_clear(mem_t*mem)
{
    rfx_free(mem->buffer);mem->buffer = 0;
}
void mem_destroy(mem_t*mem)
{
    mem_clear(mem);
    rfx_free(mem);
}
static int mem_put_(mem_t*m,const void*data, int length, int null)
{
    int n = m->pos;
    m->pos += length + (null?1:0);
    if(m->pos > m->len) { 
        m->len = (m->pos+63)&~63;
	m->buffer = m->buffer?(char*)rfx_realloc(m->buffer,m->len):(char*)rfx_alloc(m->len);
    }
    assert(n+length <= m->len);
    memcpy(&m->buffer[n], data, length);
    if(null)
	m->buffer[n + length] = 0;
    return n;
}
int mem_put(mem_t*m,void*data, int length)
{
    return mem_put_(m, data, length, 0);
}
int mem_putstring(mem_t*m,string_t str)
{
    return mem_put_(m, str.str, str.len, 1);
}

// ------------------------------- ringbuffer_t -------------------------------

typedef struct _ringbuffer_internal_t
{
    unsigned char*buffer;
    int readpos;
    int writepos;
    int buffersize;
} ringbuffer_internal_t;

void ringbuffer_init(ringbuffer_t*r)
{
    ringbuffer_internal_t*i = (ringbuffer_internal_t*)rfx_calloc(sizeof(ringbuffer_internal_t)); 
    memset(r, 0, sizeof(ringbuffer_t));
    r->internal = i;
    i->buffer = (unsigned char*)rfx_alloc(1024);
    i->buffersize = 1024;
}
int ringbuffer_read(ringbuffer_t*r, void*buf, int len)
{
    unsigned char* data = (unsigned char*)buf;
    ringbuffer_internal_t*i = (ringbuffer_internal_t*)r->internal;
    if(r->available < len)
	len = r->available;
    if(!len)
	return 0;
    if(i->readpos + len > i->buffersize) {
	int read1 = i->buffersize-i->readpos;
	memcpy(data, &i->buffer[i->readpos], read1);
	memcpy(&data[read1], &i->buffer[0], len - read1);
	i->readpos = len - read1;
    } else {
	memcpy(data, &i->buffer[i->readpos], len);
	i->readpos += len;
	i->readpos %= i->buffersize;
    }
    r->available -= len;
    return len;
}
void ringbuffer_put(ringbuffer_t*r, void*buf, int len)
{
    unsigned char* data = (unsigned char*)buf;
    ringbuffer_internal_t*i = (ringbuffer_internal_t*)r->internal;
    
    if(i->buffersize - r->available < len)
    {
	unsigned char* buf2;
	int newbuffersize = i->buffersize;
	int oldavailable = r->available;
	newbuffersize*=3;newbuffersize/=2; /*grow at least by 50% each time */

	if(newbuffersize < r->available + len)
	    newbuffersize = r->available + len + 1024;

	buf2 = (unsigned char*)rfx_alloc(newbuffersize);
	ringbuffer_read(r, buf2, r->available);
	rfx_free(i->buffer);
	i->buffer = buf2;
	i->buffersize = newbuffersize;
	i->readpos = 0;
	i->writepos = oldavailable;
	r->available = oldavailable;
    }
    if(i->writepos + len > i->buffersize) {
	int read1 = i->buffersize-i->writepos;
	memcpy(&i->buffer[i->writepos], data, read1);
	memcpy(&i->buffer[0], &data[read1], len - read1);
	i->writepos = len - read1;
    } else {
	memcpy(&i->buffer[i->writepos], data, len);
	i->writepos += len;
	i->writepos %= i->buffersize;
    }
    r->available += len;
}
void ringbuffer_clear(ringbuffer_t*r)
{
    ringbuffer_internal_t*i = (ringbuffer_internal_t*)r->internal;
    rfx_free(i->buffer);i->buffer = 0;
    rfx_free(i);
}

// ------------------------------- heap_t -------------------------------

void heap_init(heap_t*h,int n,int elem_size, int(*compare)(const void *, const void *))
{
    memset(h, 0, sizeof(heap_t));
    h->max_size = n;
    h->size = 0;
    h->elem_size = elem_size;
    h->compare = compare;
    h->elements = (void**)rfx_calloc(n*sizeof(void*));
    h->data = (char*)rfx_calloc(h->max_size*h->elem_size);
}
void heap_clear(heap_t*h)
{
    rfx_free(h->elements);
    rfx_free(h->data);
}

#define HEAP_NODE_SMALLER(h,node1,node2) ((h)->compare((node1),(node2))>0)

static void up(heap_t*h, int node)
{
    void*node_p = h->elements[node];
    int parent = node;
    do {
	node = parent;
	if(!node) break;
	parent = (node-1)/2;
	h->elements[node] = h->elements[parent];
    } while(HEAP_NODE_SMALLER(h,h->elements[parent], node_p));

    h->elements[node] = node_p;
}
static void down(heap_t*h, int node)
{
    void*node_p = h->elements[node];
    int child = node;
    do {
	node = child;

	/* determine new child's position */
	child = node<<1|1;
	if(child >= h->size) 
            break;
        if(child+1 < h->size && HEAP_NODE_SMALLER(h,h->elements[child],h->elements[child+1])) // search for bigger child
	    child++;

	h->elements[node] = h->elements[child];
    } while(HEAP_NODE_SMALLER(h,node_p, h->elements[child]));
    
    h->elements[node] = node_p;
}
void heap_put(heap_t*h, void*e) 
{
    int pos = h->size++;
    memcpy(&h->data[pos*h->elem_size],e,h->elem_size);
    h->elements[pos] = &h->data[pos];
    up(h, pos);
}
int heap_size(heap_t*h)
{
    return h->size;
}
void* heap_max(heap_t*h)
{
    return h->elements[0];
}
void* heap_chopmax(heap_t*h)
{
    void*p = h->elements[0];
    h->elements[0] = h->elements[--h->size];
    down(h,0);
    return p;
}
void heap_dump(heap_t*h, FILE*fi)
{
    int t;
    for(t=0;t<h->size;t++) {
	int s;
	for(s=0;s<=t;s=(s+1)*2-1) {
	    if(s==t) fprintf(fi,"\n");
	}
	//fprintf(fi,"%d ", h->elements[t]->x); //?
    }
}
void** heap_flatten(heap_t*h)
{
    void**nodes = (void**)rfx_alloc(h->size*sizeof(void*));
    void**p = nodes;
   
    while(h->size) {
	/*printf("Heap Size: %d\n", h->size);
	heap_print(stdout, h);
	printf("\n");*/
	*p++ = heap_chopmax(h);
    }
    return nodes;
}

// ------------------------------- crc32 --------------------------------------
static unsigned int*crc32 = 0;
static void crc32_init(void)
{
    int t;
    if(crc32) 
        return;
    crc32= (unsigned int*)rfx_alloc(sizeof(unsigned int)*256);
    for(t=0; t<256; t++) {
        unsigned int c = t;
        int s;
        for (s = 0; s < 8; s++) {
          c = (0xedb88320L*(c&1)) ^ (c >> 1);
        }
        crc32[t] = c;
    }
}
// ------------------------------- string_t -----------------------------------

void string_set2(string_t*str, const char*text, int len)
{
    str->len = len;
    str->str = text;
}
void string_set(string_t*str, const char*text)
{
    str->len = strlen(text);
    str->str = text;
}
string_t string_new(const char*text, int len)
{
    string_t s;
    s.len = len;
    s.str = text;
    return s;
}
string_t string_new2(const char*text)
{
    string_t s;
    s.len = strlen(text);
    s.str = text;
    return s;
}
char* string_cstr(string_t*str)
{
    return strdup_n(str->str, str->len);
}
unsigned int string_hash(string_t*str)
{
    int t;
    unsigned int checksum = 0;
    if(!crc32)
        crc32_init();
    for(t=0;t<str->len;t++) {
        checksum = checksum>>8 ^ crc32[(str->str[t]^checksum)&0xff];
    }
    return checksum;
}
unsigned int string_hash2(const char*str)
{
    unsigned int checksum = 0;
    const char*p = str;
    if(!crc32)
        crc32_init();
    while(*p) {
        checksum = checksum>>8 ^ crc32[(*p^checksum)&0xff];
        p++;
    }
    return checksum;
}
unsigned int string_hash3(const char*str, int len)
{
    string_t s;
    s.str = str;
    s.len = len;
    return string_hash(&s);
}
void string_dup2(string_t*str, const char*text, int len)
{
    str->len = len;
    str->str = strdup_n(text, len);
}
void string_dup(string_t*str, const char*text)
{
    str->len = strlen(text);
    str->str = strdup(text);
}
int string_equals(string_t*str, const char*text)
{
    int l = strlen(text);
    if(str->len == l && !memcmp(str->str, text, l))
	return 1;
    return 0;
}
int string_equals2(string_t*str, string_t*str2)
{
    if(str->len == str2->len && !memcmp(str->str, str2->str, str->len))
	return 1;
    return 0;
}

// ------------------------------- stringarray_t ------------------------------

typedef struct _stringlist {
    int index;
    struct _stringlist*next;
} stringlist_t;

typedef struct _stringarray_internal_t
{
    mem_t pos;
    stringlist_t**hash;
    int num;
    int hashsize;
} stringarray_internal_t;

void stringarray_init(stringarray_t*sa, int hashsize)
{
    stringarray_internal_t*s;
    int t;
    sa->internal = (stringarray_internal_t*)rfx_calloc(sizeof(stringarray_internal_t)); 
    s = (stringarray_internal_t*)sa->internal;
    mem_init(&s->pos);
    s->hash = rfx_calloc(sizeof(stringlist_t*)*hashsize);
    s->hashsize = hashsize;
}
void stringarray_put(stringarray_t*sa, string_t str)
{
    stringarray_internal_t*s = (stringarray_internal_t*)sa->internal;
    int pos;
    int hash = string_hash(&str) % s->hashsize;

    char*ss = string_cstr(&str);
    mem_put(&s->pos, &ss, sizeof(char*));

    stringlist_t*l = rfx_alloc(sizeof(stringlist_t));
    l->index = s->num;
    l->next = s->hash[hash];
    s->hash[hash] = l;

    s->num++;
}
char* stringarray_at(stringarray_t*sa, int pos)
{
    stringarray_internal_t*s = (stringarray_internal_t*)sa->internal;
    char*p;
    if(pos<0 || pos>=s->num)
	return 0;
    p = *(char**)&s->pos.buffer[pos*sizeof(char*)];
    if(p<0)
	return 0;
    return p;
}
string_t stringarray_at2(stringarray_t*sa, int pos)
{
    string_t s;
    s.str = stringarray_at(sa, pos);
    s.len = s.str?strlen(s.str):0;
    return s;
}
static stringlist_t* stringlist_del(stringarray_t*sa, stringlist_t*l, int index)
{
    stringlist_t*ll = l;
    stringlist_t*old = l;
    while(l) {
        if(index==l->index) {
            old->next = l->next;
            memset(l, 0, sizeof(stringlist_t));
            rfx_free(l);
            if(old==l)
                return 0;
            else
                return ll;
        }
        old = l;
        l = l->next;
    }
    fprintf(stderr, "Internal error: did not find string %d in hash\n", index);
    return ll;
}

void stringarray_del(stringarray_t*sa, int pos)
{
    stringarray_internal_t*s = (stringarray_internal_t*)sa->internal;
    string_t str = stringarray_at2(sa, pos);
    int hash = string_hash(&str) % s->hashsize;
    s->hash[hash] = stringlist_del(sa, s->hash[hash], pos);
    *(char**)&s->pos.buffer[pos*sizeof(char*)] = 0;
}
int stringarray_find(stringarray_t*sa, string_t* str)
{
    stringarray_internal_t*s = (stringarray_internal_t*)sa->internal;
    int hash = string_hash(str) % s->hashsize;
    int t;
    stringlist_t*l = s->hash[hash];
    //TODO: statistics
    while(l) {
        string_t s = stringarray_at2(sa, l->index);
        if(string_equals2(str, &s)) {
            return l->index;
        }
        l = l->next;
    }
    return -1;
}
void stringarray_clear(stringarray_t*sa)
{
    stringarray_internal_t*s = (stringarray_internal_t*)sa->internal;
    mem_clear(&s->pos);
    int t;
    for(t=0;t<s->hashsize;t++) {
        stringlist_t*l = s->hash[t];
        while(l) {
            stringlist_t*next = l->next;
            memset(l, 0, sizeof(stringlist_t));
            rfx_free(l);
            l = next;
        }
    }
    rfx_free(s->hash);s->hash=0;
    rfx_free(s);
}
void stringarray_destroy(stringarray_t*sa)
{
    stringarray_clear(sa);
    rfx_free(sa);
}


// ------------------------------- dictionary_t -------------------------------

#define INITIAL_SIZE 63

dict_t*dict_new()
{
    dict_t*d = rfx_alloc(sizeof(dict_t));
    dict_init(d);
    return d;
}
void dict_init(dict_t*h) 
{
    memset(h, 0, sizeof(dict_t));
    h->hashsize = INITIAL_SIZE;
    h->slots = h->hashsize?(dictentry_t**)rfx_calloc(sizeof(dictentry_t*)*h->hashsize):0;
    h->num = 0;
}
static void dict_expand(dict_t*h, int newlen)
{
    assert(h->hashsize < newlen);
    dictentry_t**newslots = (dictentry_t**)rfx_calloc(sizeof(dictentry_t*)*newlen);
    int t; 
    for(t=0;t<h->hashsize;t++) {
        dictentry_t*e = h->slots[t];
        while(e) {
            dictentry_t*next = e->next;
            unsigned int newhash = e->hash%newlen;
            e->next = newslots[newhash];
            newslots[newhash] = e;
            e = next;
        }
    }
    if(h->slots)
        rfx_free(h->slots);
    h->slots = newslots;
    h->hashsize = newlen;
}
void dict_put(dict_t*h, string_t s, void* data)
{
    unsigned int hash = string_hash(&s);
    dictentry_t*e = (dictentry_t*)rfx_alloc(sizeof(dictentry_t));

    unsigned int hash2 = string_hash(&s) % h->hashsize;
    char*sdata = rfx_alloc(s.len);
    memcpy(sdata, s.str, s.len);
    e->s = sdata;
    e->len = s.len;
    e->hash = hash; //for resizing
    e->next = h->slots[hash2];
    e->data = data;
    h->slots[hash2] = e;
    h->num++;
}
void dict_put2(dict_t*h, const char*s, void*data) 
{
    string_t s2;
    string_set(&s2, s);
    dict_put(h, s2, data);
}
void dict_put3(dict_t*h, const char* s, int len, void*data)
{
    string_t s3;
    string_set2(&s3, s, len);
    dict_put(h, s3, data);
}
void dict_dump(dict_t*h, FILE*fi, const char*prefix)
{
    int t;
    for(t=0;t<h->hashsize;t++) {
        dictentry_t*e = h->slots[t];
        while(e) {
            char*s = strdup_n(e->s, e->len);
            fprintf(fi, "%s%s=%08x\n", prefix, e->s, e->data);
            rfx_free(s);
            e = e->next;
        }
    }
}
int dict_count(dict_t*h)
{
    return h->num;
}
void* dict_lookup2(dict_t*h, const char*s, int len)
{
    if(!h->num)
        return 0;

    if(h->num*3 >= h->hashsize*2) {
        /* if dict is 2/3 filled, double the size */
        dict_expand(h, (h->hashsize+1)*2-1);
    }

    unsigned int ohash = string_hash2(s);
    unsigned int hash = ohash % h->hashsize;

    dictentry_t*e = h->slots[hash];
    while(e) {
        if(e->len == len && !memcmp(e->s, s, len)) {
            return e->data;
        }
        e = e->next;
    }
    return 0;
}
void* dict_lookup(dict_t*h, const char*s)
{
    int l = strlen(s);
    return dict_lookup2(h, s, l);
}
char dict_del(dict_t*h, const char*s)
{
    if(!h->num)
        return 0;
    unsigned int hash = string_hash2(s) % h->hashsize;
    int l = strlen(s);
    dictentry_t*head = h->slots[hash];
    dictentry_t*e = head, *prev=0;
    while(e) {
        if(e->len ==l && !memcmp(e->s, s, l)) {
            dictentry_t*next = e->next;
            rfx_free((void*)e->s);
            memset(e, 0, sizeof(dictentry_t));
            rfx_free(e);
            if(e == head) {
                h->slots[hash] = 0;
            } else {
                assert(prev);
                prev->next = next;
            }
            h->num--;
            return 1;
        }
        prev = e;
        e = e->next;
    }
    return 0;
}
void dict_foreach_keyvalue(dict_t*h, void (*runFunction)(void*data, const char*key, void*val), void*data)
{
    int t;
    for(t=0;t<h->hashsize;t++) {
        dictentry_t*e = h->slots[t];
        while(e) {
            dictentry_t*next = e->next;
            if(runFunction) {
                char*s = strdup_n(e->s, e->len); //append \0
                runFunction(data, s, e->data);
                rfx_free(s);
            }
            e = e->next;
        }
    }
}
void dict_foreach_value(dict_t*h, void (*runFunction)(void*))
{
    int t;
    for(t=0;t<h->hashsize;t++) {
        dictentry_t*e = h->slots[t];
        while(e) {
            dictentry_t*next = e->next;
            if(runFunction) {
                runFunction(e->data);
            }
            e = e->next;
        }
    }
}
void dict_free_all(dict_t*h, void (*freeFunction)(void*))
{
    int t;
    for(t=0;t<h->hashsize;t++) {
        dictentry_t*e = h->slots[t];
        while(e) {
            dictentry_t*next = e->next;
            rfx_free((void*)e->s);
            if(freeFunction) {
                freeFunction(e->data);
            }
            memset(e, 0, sizeof(dictentry_t));
            rfx_free(e);
            e = next;
        }
    }
    rfx_free(h->slots);
    memset(h, 0, sizeof(dict_t));
}
void dict_clear(dict_t*h) 
{
    dict_free_all(h, 0);
}
void dict_destroy(dict_t*dict)
{
    dict_clear(dict);
    rfx_free(dict);
}

// ------------------------------- map_t --------------------------------------

typedef struct _map_internal_t
{
    dict_t d;
} map_internal_t;

void map_init(map_t*map)
{
    map_internal_t*m;
    map->internal = (map_internal_t*)rfx_calloc(sizeof(map_internal_t));
    m = (map_internal_t*)map->internal;
    dict_init(&m->d);
}
void map_put(map_t*map, string_t t1, string_t t2)
{
    map_internal_t*m = (map_internal_t*)map->internal;
    string_t s;
    char* s1 = string_cstr(&t1);
    dict_put2(&m->d, s1, (void*)string_cstr(&t2));
    rfx_free(s1);
}
const char* map_lookup(map_t*map, const char*name)
{
    map_internal_t*m = (map_internal_t*)map->internal;
    const char*value = dict_lookup(&m->d, name);
    return value;
}
static void freestring(void*data)
{
    rfx_free(data);
}
static void dumpmapentry(void*data, const char*key, void*value)
{
    FILE*fi = (FILE*)data;
    fprintf(fi, "%s=%s\n", key, (char*)value);
}
void map_dump(map_t*map, FILE*fi, const char*prefix)
{
    int t;
    map_internal_t*m = (map_internal_t*)map->internal;
    dict_foreach_keyvalue(&m->d, dumpmapentry, fi);
}
void map_clear(map_t*map)
{
    map_internal_t*m = (map_internal_t*)map->internal;
    dict_free_all(&m->d, freestring);
    rfx_free(m);
}
void map_destroy(map_t*map)
{
    map_clear(map);
    rfx_free(map);
}

