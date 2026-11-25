/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * skm_core: veja skm_core.h.
 */

#include "skm_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const meas_names[SKM_N_MEAS] = {
    [SKM_PRB_DL] = "RRU.PrbTotDl",
    [SKM_PRB_UL] = "RRU.PrbTotUl",
    [SKM_THP_DL] = "DRB.UEThpDl",
    [SKM_THP_UL] = "DRB.UEThpUl",
};

/// chaves do JSON e rótulos do texto, na ordem de skm_meas_t
static const char *const meas_keys[SKM_N_MEAS] = {
    [SKM_PRB_DL] = "prb_dl_pct",
    [SKM_PRB_UL] = "prb_ul_pct",
    [SKM_THP_DL] = "thp_dl_kbps",
    [SKM_THP_UL] = "thp_ul_kbps",
};

bool skm_parse_snssai(const char *text, skm_snssai_t *out)
{
  if (text == NULL || out == NULL || *text == '\0')
    return false;

  char *end = NULL;
  const unsigned long sst = strtoul(text, &end, 10);
  if (end == text || sst > 255)
    return false;

  uint32_t sd = SKM_SD_NONE;
  if (*end == '.') {
    const char *sd_text = end + 1;
    if (strlen(sd_text) != 6)
      return false;
    const unsigned long v = strtoul(sd_text, &end, 16);
    if (end != sd_text + 6 || v > SKM_SD_NONE)
      return false;
    sd = (uint32_t)v;
  }
  if (*end != '\0')
    return false;

  out->sst = (uint8_t)sst;
  out->sd = sd;
  return true;
}

void skm_format_snssai(skm_snssai_t snssai, char *buf, size_t len)
{
  if (snssai.sd == SKM_SD_NONE)
    snprintf(buf, len, "%u", (unsigned)snssai.sst);
  else
    snprintf(buf, len, "%u.%06x", (unsigned)snssai.sst, (unsigned)snssai.sd);
}

size_t skm_snssai_octets(skm_snssai_t snssai, uint8_t out[4])
{
  out[0] = snssai.sst;
  if (snssai.sd == SKM_SD_NONE)
    return 1;
  out[1] = (uint8_t)(snssai.sd >> 16);
  out[2] = (uint8_t)(snssai.sd >> 8);
  out[3] = (uint8_t)snssai.sd;
  return 4;
}

const char *skm_meas_name(skm_meas_t meas)
{
  return (meas >= 0 && meas < SKM_N_MEAS) ? meas_names[meas] : NULL;
}

int skm_meas_from_name(const char *name)
{
  if (name == NULL)
    return -1;
  for (int m = 0; m < SKM_N_MEAS; m++)
    if (strcmp(name, meas_names[m]) == 0)
      return m;
  return -1;
}

void skm_slice_init(skm_slice_t *slice, skm_snssai_t snssai)
{
  memset(slice, 0, sizeof(*slice));
  slice->snssai = snssai;
}

static bool known(const skm_slice_t *slice, uint64_t ue_key)
{
  for (size_t i = 0; i < slice->n_known; i++)
    if (slice->known[i] == ue_key)
      return true;
  return false;
}

skm_slice_report_t skm_aggregate(skm_slice_t *slice, int64_t ts_us, const skm_ue_sample_t *ues, size_t n)
{
  skm_slice_report_t r = {.ts_us = ts_us, .snssai = slice->snssai};
  uint64_t now[SKM_MAX_UES];
  size_t n_now = 0;

  for (size_t i = 0; i < n; i++) {
    const skm_ue_sample_t *ue = &ues[i];
    if (n_now < SKM_MAX_UES)
      now[n_now++] = ue->ue_key;
    if (!known(slice, ue->ue_key)) {
      r.new_ues++;
      continue;
    }
    r.ues++;
    for (int m = 0; m < SKM_N_MEAS; m++) {
      if (!ue->has[m])
        continue;
      r.has[m] = true;
      r.sum[m] += ue->value[m];
    }
  }

  memcpy(slice->known, now, n_now * sizeof(now[0]));
  slice->n_known = n_now;
  return r;
}

int skm_report_text(const skm_slice_report_t *report, char *buf, size_t len)
{
  char name[16];
  skm_format_snssai(report->snssai, name, sizeof(name));

  int w = snprintf(buf, len, "slice %-9s UEs %d (novos %d)", name, report->ues, report->new_ues);
  for (int m = 0; m < SKM_N_MEAS; m++) {
    if (w < 0 || (size_t)w >= len)
      return w;
    if (report->has[m])
      w += snprintf(buf + w, len - (size_t)w, "  %s %.1f", meas_keys[m], report->sum[m]);
  }
  return w;
}

int skm_report_json(const skm_slice_report_t *report, char *buf, size_t len)
{
  char name[16];
  skm_format_snssai(report->snssai, name, sizeof(name));

  int w = snprintf(buf,
                   len,
                   "{\"ts_us\":%lld,\"slice\":\"%s\",\"ues\":%d,\"new_ues\":%d",
                   (long long)report->ts_us,
                   name,
                   report->ues,
                   report->new_ues);
  for (int m = 0; m < SKM_N_MEAS; m++) {
    if (w < 0 || (size_t)w >= len)
      return w;
    if (report->has[m])
      w += snprintf(buf + w, len - (size_t)w, ",\"%s\":%.3f", meas_keys[m], report->sum[m]);
  }
  if (w >= 0 && (size_t)w < len)
    w += snprintf(buf + w, len - (size_t)w, "}");
  return w;
}
