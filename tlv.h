#ifndef _TLV_H
#define _TLV_H

// tools for TLV(Type-Length-Value) function

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif


struct tlv {
    uint16_t type;
    uint16_t len;
    uint8_t *value;
    uint16_t total_len;
};

void tlv_init(struct tlv *tlv);

/*
 * @function tlv_parse - parse buf, save to tlv instance
 * @return 0:ok, -1:invalid param, -2:malformed
 */
int tlv_parse(struct tlv *tlv, uint8_t *buf, int len);

//parse a raw from buf to tlv
int tlv_raw_parse(struct tlv *tlv, uint8_t *buf, int len, int index);

int tlv_set(struct tlv *tlv, uint16_t type, uint8_t *value, uint16_t len);
int tlv_set_string(struct tlv *tlv, uint16_t type, const char *string);

int tlv_put(struct tlv *tlv, uint8_t *buf, int len);

int tlv_append(struct tlv *tlv, uint8_t *buf, int len, int index);

// 正确返回put的长度
int tlv_raw_put(uint8_t *buf, int len, uint16_t type, uint8_t *value, uint16_t vlen);

//append a raw to buf
int tlv_raw_append(uint8_t *buf, int len, uint16_t type, uint8_t *value, uint16_t vlen, int index);

void tlv_clear(struct tlv *tlv);

#ifdef __cplusplus
}
#endif

#endif
