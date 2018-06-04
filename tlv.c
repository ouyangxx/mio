#include <stdio.h>
#include <string.h>

#ifdef WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#include "tlv.h"

#define TLV_PAD(n) (((n) % 4) ? ((n) + (4 - ((n) % 4))) : (n))


void tlv_init(struct tlv *tlv)
{
    if (tlv!=NULL) {
        memset(tlv, 0, sizeof(*tlv));
    }
}

int tlv_parse(struct tlv *tlv, uint8_t *buf, int len)
{
    if (tlv==NULL || buf==NULL) {
        return -1;
    }
    int iter=0;
    if (len-iter<sizeof(tlv->type)) {
        return -2;
    }
    tlv->type = ntohs(*(uint16_t *)&buf[iter]); iter+=sizeof(tlv->type);
    if (len-iter<sizeof(tlv->len)) {
        return -2;
    }
    tlv->len = ntohs(*(uint16_t *)&buf[iter]); iter+=sizeof(tlv->len);
    int padding_len = TLV_PAD(tlv->len);
    if (len-iter < padding_len) {
        return -2;
    }
    tlv->value = &buf[iter];
    tlv->total_len = sizeof(tlv->type)+sizeof(tlv->len)+padding_len;
    return 0;
}

int tlv_raw_parse(struct tlv *tlv, uint8_t *buf, int len, int index)
{
	if (tlv == NULL || buf == NULL) {
		return -1;
	}
	int iter = index;
	if (len - iter<sizeof(tlv->type)) {
		return -2;
	}
	tlv->type = ntohs(*(uint16_t *)&buf[iter]); iter += sizeof(tlv->type);
	if (len - iter<sizeof(tlv->len)) {
		return -2;
	}
	tlv->len = ntohs(*(uint16_t *)&buf[iter]); iter += sizeof(tlv->len);
	int padding_len = TLV_PAD(tlv->len);
	if (len - iter < padding_len) {
		return -2;
	}
	tlv->value = &buf[iter];
	tlv->total_len = sizeof(tlv->type) + sizeof(tlv->len) + padding_len;
	return 0;
}

int tlv_set(struct tlv *tlv, uint16_t type, uint8_t *value, uint16_t len)
{
    if (tlv==NULL || value==NULL || len==0) {
        return -1;
    }
    tlv->type = type;
    tlv->len = len;
    tlv->value = value;
    tlv->total_len = sizeof(tlv->type)+sizeof(tlv->len)+TLV_PAD(tlv->len);
    return 0;
}

int tlv_put(struct tlv *tlv, uint8_t *buf, int len)
{
    if (tlv==NULL || tlv->total_len==0 || tlv->value==NULL || buf==NULL) {
        return -1;
    }
    if (len<tlv->total_len) {
        return -2;
    }
    int iter=0;
    *(uint16_t *)(buf+iter) = htons(tlv->type); iter+=sizeof(uint16_t);
    *(uint16_t *)(buf+iter) = htons(tlv->len); iter+=sizeof(uint16_t);
    memcpy((void *)(buf+iter), (void *)tlv->value, tlv->len);
    return 0;
}

int tlv_append(struct tlv *tlv, uint8_t *buf, int len, int index)
{
	if (tlv == NULL || tlv->total_len == 0 || tlv->value == NULL || buf == NULL) {
		return -1;
	}
	if (len<tlv->total_len) {
		return -2;
	}
	int iter = index;
	*(uint16_t *)(buf + iter) = htons(tlv->type); iter += sizeof(uint16_t);
	*(uint16_t *)(buf + iter) = htons(tlv->len); iter += sizeof(uint16_t);
	memcpy((void *)(buf + iter), (void *)tlv->value, tlv->len);
	return 0;
}

int tlv_raw_put(uint8_t *buf, int len, uint16_t type, uint8_t *value, uint16_t vlen)
{
    struct tlv tlv;
    tlv_init(&tlv);
    int ret = tlv_set(&tlv, type, value, vlen);
    if (ret!=0) {
        return ret;
    }
    ret = tlv_put(&tlv, buf, len);
    if (ret!=0) {
        return ret;
    }
    return tlv.total_len;
}

int tlv_raw_append(uint8_t *buf, int len, uint16_t type, uint8_t *value, uint16_t vlen, int index)
{
	struct tlv tlv;
	tlv_init(&tlv);
	int ret = tlv_set(&tlv, type, value, vlen);
	if (ret != 0) {
		return ret;
	}
	ret = tlv_append(&tlv, buf, len, index);
	if (ret != 0) {
		return ret;
	}
	return tlv.total_len;
}

int tlv_set_string(struct tlv *tlv, uint16_t type, const char *string)
{
    if (string==NULL) {
        return -1;
    }
    int len = strlen(string);
    return tlv_set(tlv, type, (uint8_t *)string, len);
}

void tlv_clear(struct tlv *tlv)
{
    tlv_init(tlv);
}
