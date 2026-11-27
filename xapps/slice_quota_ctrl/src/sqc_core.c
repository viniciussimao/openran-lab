/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sqc_core.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/// inteiro decimal sem sinal até max; avança *p
static bool read_dec(const char **p, unsigned long max, unsigned long *out)
{
  const char *s = *p;
  if (!isdigit((unsigned char)*s))
    return false;
  unsigned long v = 0;
  while (isdigit((unsigned char)*s)) {
    v = v * 10 + (unsigned long)(*s - '0');
    if (v > max)
      return false;
    s++;
  }
  *p = s;
  *out = v;
  return true;
}

bool sqc_parse_quota(const char *text, sqc_quota_t *out)
{
  if (text == NULL)
    return false;
  const char *p = text;
  unsigned long sst;
  if (!read_dec(&p, 255, &sst))
    return false;

  uint32_t sd = SQC_SD_NONE;
  if (*p == '.') {
    p++;
    sd = 0;
    for (int i = 0; i < 6; i++, p++) {
      if (!isxdigit((unsigned char)*p))
        return false;
      const char c = (char)tolower((unsigned char)*p);
      sd = (sd << 4) | (uint32_t)(isdigit((unsigned char)c) ? c - '0' : c - 'a' + 10);
    }
  }

  unsigned long r[3];
  for (int i = 0; i < 3; i++) {
    if (*p != ':')
      return false;
    p++;
    if (!read_dec(&p, 100, &r[i]))
      return false;
  }
  if (*p != '\0')
    return false;
  /* min, max, ded na linha de comando; ded <= min <= max */
  if (!(r[2] <= r[0] && r[0] <= r[1]))
    return false;

  out->sst = (uint8_t)sst;
  out->sd = sd;
  out->min_ratio = (uint8_t)r[0];
  out->max_ratio = (uint8_t)r[1];
  out->dedicated_ratio = (uint8_t)r[2];
  return true;
}

bool sqc_parse_remove(const char *snssai, sqc_quota_t *out)
{
  if (snssai == NULL || strchr(snssai, ':') != NULL)
    return false;
  char buf[32];
  const int n = snprintf(buf, sizeof(buf), "%s:0:100:0", snssai);
  if (n < 0 || (size_t)n >= sizeof(buf))
    return false;
  return sqc_parse_quota(buf, out);
}

bool sqc_quota_is_neutral(const sqc_quota_t *q)
{
  return q->min_ratio == 0 && q->max_ratio == 100 && q->dedicated_ratio == 0;
}

bool sqc_parse_plmn(const char *text, sqc_plmn_t *out)
{
  if (text == NULL)
    return false;
  const char *p = text;
  const char *start = p;
  unsigned long mcc, mnc;
  if (!read_dec(&p, 999, &mcc) || p - start != 3 || *p != '.')
    return false;
  p++;
  start = p;
  if (!read_dec(&p, 999, &mnc) || *p != '\0')
    return false;
  const long digits = p - start;
  if (digits != 2 && digits != 3)
    return false;
  out->mcc = (uint16_t)mcc;
  out->mnc = (uint16_t)mnc;
  out->mnc_digit_len = (uint8_t)digits;
  return true;
}

void sqc_plmn_octets(sqc_plmn_t plmn, uint8_t out[3])
{
  const unsigned mcc1 = plmn.mcc / 100, mcc2 = plmn.mcc / 10 % 10, mcc3 = plmn.mcc % 10;
  unsigned mnc1, mnc2, mnc3;
  if (plmn.mnc_digit_len == 3) {
    mnc1 = plmn.mnc / 100;
    mnc2 = plmn.mnc / 10 % 10;
    mnc3 = plmn.mnc % 10;
  } else {
    mnc1 = plmn.mnc / 10;
    mnc2 = plmn.mnc % 10;
    mnc3 = 0xf;
  }
  out[0] = (uint8_t)((mcc2 << 4) | mcc1);
  out[1] = (uint8_t)((mnc3 << 4) | mcc3);
  out[2] = (uint8_t)((mnc2 << 4) | mnc1);
}

void sqc_sd_octets(uint32_t sd, uint8_t out[3])
{
  out[0] = (uint8_t)(sd >> 16);
  out[1] = (uint8_t)(sd >> 8);
  out[2] = (uint8_t)sd;
}

bool sqc_validate(const sqc_quota_t *quotas, size_t n, char *err, size_t err_len)
{
  char dummy[1];
  if (err == NULL || err_len == 0) {
    err = dummy;
    err_len = sizeof(dummy);
  }
  if (n > SQC_MAX_QUOTAS) {
    snprintf(err, err_len, "%zu cotas, mais que %d", n, SQC_MAX_QUOTAS);
    return false;
  }
  unsigned sum_min = 0;
  for (size_t i = 0; i < n; i++) {
    const sqc_quota_t *q = &quotas[i];
    char name[64];
    sqc_format_quota(q, name, sizeof(name));
    if (q->sd > SQC_SD_NONE || q->max_ratio > 100 || q->min_ratio > q->max_ratio || q->dedicated_ratio > q->min_ratio) {
      snprintf(err, err_len, "cota inválida (precisa de ded <= min <= max <= 100): %s", name);
      return false;
    }
    for (size_t j = 0; j < i; j++) {
      if (quotas[j].sst == q->sst && quotas[j].sd == q->sd) {
        snprintf(err, err_len, "S-NSSAI repetida: %s", name);
        return false;
      }
    }
    sum_min += q->min_ratio;
  }
  if (sum_min > 100) {
    snprintf(err, err_len, "a soma dos min_ratio é %u, mais que 100", sum_min);
    return false;
  }
  return true;
}

int sqc_format_quota(const sqc_quota_t *q, char *buf, size_t len)
{
  if (q->sd == SQC_SD_NONE)
    return snprintf(buf, len, "%u min %u max %u ded %u", q->sst, q->min_ratio, q->max_ratio, q->dedicated_ratio);
  return snprintf(buf,
                  len,
                  "%u.%06x min %u max %u ded %u",
                  q->sst,
                  (unsigned)q->sd,
                  q->min_ratio,
                  q->max_ratio,
                  q->dedicated_ratio);
}
