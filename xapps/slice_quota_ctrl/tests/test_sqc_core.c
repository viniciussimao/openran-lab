/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Testes do sqc_core, sem RIC. Rode com ctest ou direto: ./test_sqc_core
 */

#include "sqc_core.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      fprintf(stderr, "%s:%d: falhou: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                         \
    }                                                                     \
  } while (0)

static void test_parse_quota(void)
{
  sqc_quota_t q;
  CHECK(sqc_parse_quota("1.000001:0:70:0", &q));
  CHECK(q.sst == 1 && q.sd == 1 && q.min_ratio == 0 && q.max_ratio == 70 && q.dedicated_ratio == 0);
  CHECK(sqc_parse_quota("1.00000A:40:100:20", &q) && q.sd == 0xa && q.min_ratio == 40 && q.dedicated_ratio == 20);
  CHECK(sqc_parse_quota("2:10:30:5", &q) && q.sst == 2 && q.sd == SQC_SD_NONE);
  CHECK(sqc_parse_quota("255.ffffff:0:100:0", &q) && q.sd == SQC_SD_NONE);

  CHECK(!sqc_parse_quota("", &q));
  CHECK(!sqc_parse_quota("1.000001:0:70", &q));
  CHECK(!sqc_parse_quota("1.000001:0:70:0:1", &q));
  CHECK(!sqc_parse_quota("1.0001:0:70:0", &q));
  CHECK(!sqc_parse_quota("256.000001:0:70:0", &q));
  CHECK(!sqc_parse_quota("1.000001:0:101:0", &q));
  CHECK(!sqc_parse_quota("1.000001:50:40:0", &q)); /* min > max */
  CHECK(!sqc_parse_quota("1.000001:10:40:20", &q)); /* ded > min */
  CHECK(!sqc_parse_quota("1.000001:-1:40:0", &q));
  CHECK(!sqc_parse_quota(NULL, &q));
}

static void test_plmn(void)
{
  sqc_plmn_t p;
  uint8_t o[3];
  CHECK(sqc_parse_plmn("001.01", &p) && p.mcc == 1 && p.mnc == 1 && p.mnc_digit_len == 2);
  sqc_plmn_octets(p, o);
  CHECK(o[0] == 0x00 && o[1] == 0xf1 && o[2] == 0x10);

  CHECK(sqc_parse_plmn("310.410", &p) && p.mcc == 310 && p.mnc == 410 && p.mnc_digit_len == 3);
  sqc_plmn_octets(p, o);
  CHECK(o[0] == 0x13 && o[1] == 0x00 && o[2] == 0x14);

  CHECK(sqc_parse_plmn("208.95", &p));
  sqc_plmn_octets(p, o);
  CHECK(o[0] == 0x02 && o[1] == 0xf8 && o[2] == 0x59);

  CHECK(!sqc_parse_plmn("01.01", &p));
  CHECK(!sqc_parse_plmn("001.1", &p));
  CHECK(!sqc_parse_plmn("001.0101", &p));
  CHECK(!sqc_parse_plmn("00101", &p));
}

static void test_sd_octets(void)
{
  uint8_t o[3];
  sqc_sd_octets(0x0a0b0c, o);
  CHECK(o[0] == 0x0a && o[1] == 0x0b && o[2] == 0x0c);
}

static void test_validate(void)
{
  char err[128];
  sqc_quota_t q[3];
  CHECK(sqc_parse_quota("1.000001:0:70:0", &q[0]));
  CHECK(sqc_parse_quota("1.000002:0:30:0", &q[1]));
  CHECK(sqc_validate(q, 2, err, sizeof(err)));
  CHECK(sqc_validate(q, 0, err, sizeof(err)));

  CHECK(sqc_parse_quota("1.000001:60:100:0", &q[0]));
  CHECK(sqc_parse_quota("1.000002:60:100:0", &q[1]));
  CHECK(!sqc_validate(q, 2, err, sizeof(err)));
  CHECK(strstr(err, "soma") != NULL);

  CHECK(sqc_parse_quota("1.000001:0:50:0", &q[0]));
  CHECK(sqc_parse_quota("1.000001:0:40:0", &q[1]));
  CHECK(!sqc_validate(q, 2, err, sizeof(err)));
  CHECK(strstr(err, "repetida") != NULL);

  q[2] = q[0];
  q[2].dedicated_ratio = 10; /* ded > min */
  CHECK(!sqc_validate(&q[2], 1, NULL, 0));

  sqc_quota_t many[SQC_MAX_QUOTAS + 1];
  for (int i = 0; i <= SQC_MAX_QUOTAS; i++)
    many[i] = (sqc_quota_t){.sst = 1, .sd = (uint32_t)i, .max_ratio = 10};
  CHECK(sqc_validate(many, SQC_MAX_QUOTAS, err, sizeof(err)));
  CHECK(!sqc_validate(many, SQC_MAX_QUOTAS + 1, err, sizeof(err)));
}

static void test_format(void)
{
  char buf[64];
  sqc_quota_t q;
  CHECK(sqc_parse_quota("1.000002:10:30:5", &q));
  sqc_format_quota(&q, buf, sizeof(buf));
  CHECK(strcmp(buf, "1.000002 min 10 max 30 ded 5") == 0);
  CHECK(sqc_parse_quota("3:0:100:0", &q));
  sqc_format_quota(&q, buf, sizeof(buf));
  CHECK(strcmp(buf, "3 min 0 max 100 ded 0") == 0);
}

int main(void)
{
  test_parse_quota();
  test_plmn();
  test_sd_octets();
  test_validate();
  test_format();
  if (failures == 0)
    printf("sqc_core: todos os testes passaram\n");
  return failures == 0 ? 0 : 1;
}
