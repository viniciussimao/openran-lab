/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Testes do skm_core, sem RIC. Rode com ctest ou direto: ./test_skm_core
 */

#include "skm_core.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                \
  do {                                                             \
    if (!(cond)) {                                                 \
      fprintf(stderr, "%s:%d: falhou: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                  \
    }                                                              \
  } while (0)

static skm_ue_sample_t ue(uint64_t key, double prb_dl, double thp_dl)
{
  skm_ue_sample_t s = {.ue_key = key};
  s.has[SKM_PRB_DL] = true;
  s.value[SKM_PRB_DL] = prb_dl;
  s.has[SKM_THP_DL] = true;
  s.value[SKM_THP_DL] = thp_dl;
  return s;
}

static void test_parse_snssai(void)
{
  skm_snssai_t s;
  CHECK(skm_parse_snssai("1.000001", &s) && s.sst == 1 && s.sd == 1);
  CHECK(skm_parse_snssai("1.00000a", &s) && s.sd == 0xa);
  CHECK(skm_parse_snssai("255.ffffff", &s) && s.sst == 255 && s.sd == SKM_SD_NONE);
  CHECK(skm_parse_snssai("2", &s) && s.sst == 2 && s.sd == SKM_SD_NONE);
  CHECK(!skm_parse_snssai("", &s));
  CHECK(!skm_parse_snssai("256.000001", &s));
  CHECK(!skm_parse_snssai("1.0001", &s));
  CHECK(!skm_parse_snssai("1.0000001", &s));
  CHECK(!skm_parse_snssai("1.00000g", &s));
  CHECK(!skm_parse_snssai("1x", &s));
  CHECK(!skm_parse_snssai(".000001", &s));
}

static void test_format_and_octets(void)
{
  char buf[16];
  uint8_t o[4];
  skm_format_snssai((skm_snssai_t){1, 2}, buf, sizeof(buf));
  CHECK(strcmp(buf, "1.000002") == 0);
  skm_format_snssai((skm_snssai_t){1, SKM_SD_NONE}, buf, sizeof(buf));
  CHECK(strcmp(buf, "1") == 0);
  CHECK(skm_snssai_octets((skm_snssai_t){1, 0x0a0b0c}, o) == 4);
  CHECK(o[0] == 1 && o[1] == 0x0a && o[2] == 0x0b && o[3] == 0x0c);
  CHECK(skm_snssai_octets((skm_snssai_t){3, SKM_SD_NONE}, o) == 1 && o[0] == 3);
}

static void test_meas_names(void)
{
  CHECK(skm_meas_from_name("RRU.PrbTotDl") == SKM_PRB_DL);
  CHECK(skm_meas_from_name("DRB.UEThpUl") == SKM_THP_UL);
  CHECK(skm_meas_from_name("DRB.PdcpSduVolumeDL") == -1);
  CHECK(strcmp(skm_meas_name(SKM_THP_DL), "DRB.UEThpDl") == 0);
}

static void test_first_report_discarded(void)
{
  skm_slice_t sl;
  skm_slice_init(&sl, (skm_snssai_t){1, 1});
  skm_ue_sample_t s[] = {ue(10, 99, 1e6)};

  skm_slice_report_t r = skm_aggregate(&sl, 1, s, 1);
  CHECK(r.ues == 0 && r.new_ues == 1 && !r.has[SKM_PRB_DL]);

  s[0] = ue(10, 22, 40000);
  r = skm_aggregate(&sl, 2, s, 1);
  CHECK(r.ues == 1 && r.new_ues == 0);
  CHECK(r.has[SKM_PRB_DL] && fabs(r.sum[SKM_PRB_DL] - 22) < 1e-9);
  CHECK(!r.has[SKM_PRB_UL]);
}

static void test_sum_and_ue_churn(void)
{
  skm_slice_t sl;
  skm_slice_init(&sl, (skm_snssai_t){1, 2});
  skm_ue_sample_t a[] = {ue(1, 10, 100), ue(2, 20, 200)};
  skm_aggregate(&sl, 1, a, 2);

  skm_slice_report_t r = skm_aggregate(&sl, 2, a, 2);
  CHECK(r.ues == 2 && fabs(r.sum[SKM_PRB_DL] - 30) < 1e-9 && fabs(r.sum[SKM_THP_DL] - 300) < 1e-9);

  /* UE 2 sai, UE 3 entra: UE 3 é descartado neste relatório */
  skm_ue_sample_t b[] = {ue(1, 11, 110), ue(3, 50, 500)};
  r = skm_aggregate(&sl, 3, b, 2);
  CHECK(r.ues == 1 && r.new_ues == 1 && fabs(r.sum[SKM_PRB_DL] - 11) < 1e-9);

  /* UE 2 volta: conta como novo de novo */
  skm_ue_sample_t c[] = {ue(2, 5, 50), ue(3, 40, 400)};
  r = skm_aggregate(&sl, 4, c, 2);
  CHECK(r.ues == 1 && r.new_ues == 1 && fabs(r.sum[SKM_PRB_DL] - 40) < 1e-9);
}

static void test_empty_report(void)
{
  skm_slice_t sl;
  skm_slice_init(&sl, (skm_snssai_t){1, 1});
  skm_ue_sample_t s[] = {ue(1, 1, 1)};
  skm_aggregate(&sl, 1, s, 1);
  skm_slice_report_t r = skm_aggregate(&sl, 2, NULL, 0);
  CHECK(r.ues == 0 && r.new_ues == 0 && sl.n_known == 0);
}

static void test_output(void)
{
  skm_slice_t sl;
  skm_slice_init(&sl, (skm_snssai_t){1, 1});
  skm_ue_sample_t s[] = {ue(7, 22, 39100.5)};
  skm_aggregate(&sl, 100, s, 1);
  skm_slice_report_t r = skm_aggregate(&sl, 1700000000000000LL, s, 1);

  char buf[256];
  skm_report_json(&r, buf, sizeof(buf));
  CHECK(strcmp(buf,
               "{\"ts_us\":1700000000000000,\"slice\":\"1.000001\",\"ues\":1,\"new_ues\":0,"
               "\"prb_dl_pct\":22.000,\"thp_dl_kbps\":39100.500}")
        == 0);

  skm_report_text(&r, buf, sizeof(buf));
  CHECK(strstr(buf, "slice 1.000001") == buf);
  CHECK(strstr(buf, "prb_dl_pct 22.0") != NULL);

  /* buffer curto: não estoura e devolve o tamanho que precisaria */
  char tiny[8];
  CHECK(skm_report_json(&r, tiny, sizeof(tiny)) >= (int)sizeof(tiny));
  CHECK(tiny[sizeof(tiny) - 1] == '\0');
}

int main(void)
{
  test_parse_snssai();
  test_format_and_octets();
  test_meas_names();
  test_first_report_discarded();
  test_sum_and_ue_churn();
  test_empty_report();
  test_output();
  if (failures == 0)
    printf("skm_core: todos os testes passaram\n");
  return failures == 0 ? 0 : 1;
}
