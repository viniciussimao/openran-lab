/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * slice_kpm_monitor: xApp de monitoração KPM por slice, adaptador FlexRIC.
 *
 * Para cada slice pedido, assina no E2 node o REPORT estilo 4 do E2SM-KPM
 * (medidas por UE com condição), com a S-NSSAI de 4 octetos (SST + SD) e
 * as medidas RRU.PrbTotDl/Ul e DRB.UEThpDl/Ul que o nó oferecer. A cada
 * indicação, converte as medidas por UE para skm_ue_sample_t e entrega ao
 * skm_core, que soma o slice e escreve o resultado.
 *
 * Uso:
 *   slice_kpm_monitor --slice 1.000001 --slice 1.000002 [--json ARQ]
 *                     [--per-ue] [--period-ms N] [opções do FlexRIC]
 *
 *   --slice S      S-NSSAI "SST.SD" (SD com 6 dígitos hex) ou "SST"; repita
 *                  para cada slice (até SKM_MAX_SLICES)
 *   --json ARQ     acrescenta uma linha JSON por slice por relatório em ARQ
 *   --per-ue       imprime também as medidas de cada UE
 *   --period-ms N  período do relatório (padrão 1000)
 *   Outras opções (por exemplo, -c arquivo.conf) vão para o FlexRIC.
 *   XAPP_DURATION=segundos encerra a xApp sozinha, como nos exemplos.
 *
 * O primeiro relatório de cada UE é descartado (veja skm_core.h). Um slice
 * sem UE não gera relatório: o agente E2 do OAI não envia indicação vazia.
 */

#include "skm_core.h"

#include "xApp/e42_xapp_api.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SKM_MAX_SLICES 8
#define SKM_KPM_RAN_FUNC_ID 2

static skm_slice_t slices[SKM_MAX_SLICES];
static size_t n_slices;
static FILE *json_out;
static bool per_ue;
static uint32_t period_ms = 1000;
static pthread_mutex_t out_mtx = PTHREAD_MUTEX_INITIALIZER;

/* ---- Indicações --------------------------------------------------------- */

/// identificador estável do UE no E2: o RAN UE ID, se houver
static uint64_t ue_key(const ue_id_e2sm_t *id)
{
  switch (id->type) {
    case GNB_UE_ID_E2SM:
      return id->gnb.ran_ue_id ? *id->gnb.ran_ue_id : id->gnb.amf_ue_ngap_id;
    case GNB_DU_UE_ID_E2SM:
      return id->gnb_du.ran_ue_id ? *id->gnb_du.ran_ue_id : id->gnb_du.gnb_cu_ue_f1ap;
    case GNB_CU_UP_UE_ID_E2SM:
      return id->gnb_cu_up.ran_ue_id ? *id->gnb_cu_up.ran_ue_id : id->gnb_cu_up.gnb_cu_cp_ue_e1ap;
    default:
      return UINT64_MAX;
  }
}

/// medidas de um UE: cada medida pedida tem um rótulo só (noLabel)
static skm_ue_sample_t ue_sample(const meas_report_per_ue_t *rep)
{
  skm_ue_sample_t s = {.ue_key = ue_key(&rep->ue_meas_report_lst)};
  const kpm_ind_msg_format_1_t *f1 = &rep->ind_msg_format_1;
  if (f1->meas_data_lst_len == 0)
    return s;
  const meas_data_lst_t *data = &f1->meas_data_lst[0];

  size_t rec = 0;
  for (size_t i = 0; i < f1->meas_info_lst_len; i++) {
    const meas_info_format_1_lst_t *info = &f1->meas_info_lst[i];
    for (size_t l = 0; l < info->label_info_lst_len; l++, rec++) {
      if (rec >= data->meas_record_len || info->meas_type.type != NAME_MEAS_TYPE)
        continue;
      char *name = cp_ba_to_str(info->meas_type.name);
      const int m = skm_meas_from_name(name);
      free(name);
      if (m < 0)
        continue;
      const meas_record_lst_t *r = &data->meas_record_lst[rec];
      if (r->value == INTEGER_MEAS_VALUE) {
        s.has[m] = true;
        s.value[m] = r->int_val;
      } else if (r->value == REAL_MEAS_VALUE) {
        s.has[m] = true;
        s.value[m] = r->real_val;
      }
    }
  }
  return s;
}

static void on_indication(size_t slice_idx, sm_ag_if_rd_t const *rd)
{
  if (rd == NULL || rd->type != INDICATION_MSG_AGENT_IF_ANS_V0 || rd->ind.type != KPM_STATS_V3_0)
    return;
  const kpm_ind_data_t *ind = &rd->ind.kpm.ind;
  if (ind->msg.type != FORMAT_3_INDICATION_MESSAGE)
    return;

  const kpm_ind_msg_format_3_t *f3 = &ind->msg.frm_3;
  skm_ue_sample_t ues[SKM_MAX_UES];
  size_t n = 0;
  for (size_t i = 0; i < f3->ue_meas_report_lst_len && n < SKM_MAX_UES; i++)
    ues[n++] = ue_sample(&f3->meas_report_per_ue[i]);

  const int64_t ts = (int64_t)ind->hdr.kpm_ric_ind_hdr_format_1.collectStartTime;

  pthread_mutex_lock(&out_mtx);
  skm_slice_report_t r = skm_aggregate(&slices[slice_idx], ts, ues, n);
  char line[512];
  skm_report_text(&r, line, sizeof(line));
  printf("%s\n", line);
  if (per_ue) {
    for (size_t i = 0; i < n; i++) {
      printf("  UE %" PRIu64 ":", ues[i].ue_key);
      for (int m = 0; m < SKM_N_MEAS; m++)
        if (ues[i].has[m])
          printf(" %s %.1f", skm_meas_name(m), ues[i].value[m]);
      printf("\n");
    }
  }
  fflush(stdout);
  if (json_out != NULL) {
    skm_report_json(&r, line, sizeof(line));
    fprintf(json_out, "%s\n", line);
    fflush(json_out);
  }
  pthread_mutex_unlock(&out_mtx);
}

/* o callback do FlexRIC não recebe contexto: uma função por slice */
#define SKM_CB(i) \
  static void cb_##i(sm_ag_if_rd_t const *rd) { on_indication(i, rd); }
SKM_CB(0)
SKM_CB(1)
SKM_CB(2)
SKM_CB(3)
SKM_CB(4)
SKM_CB(5)
SKM_CB(6)
SKM_CB(7)
static const sm_cb slice_cb[SKM_MAX_SLICES] = {cb_0, cb_1, cb_2, cb_3, cb_4, cb_5, cb_6, cb_7};

/* ---- Assinatura --------------------------------------------------------- */

static void *xcalloc(size_t n, size_t sz)
{
  void *p = calloc(n, sz);
  assert(p != NULL && "memória esgotada");
  return p;
}

/// condição "S-NSSAI igual a" com a S-NSSAI de 1 ou 4 octetos
static test_info_lst_t snssai_condition(skm_snssai_t snssai)
{
  test_info_lst_t t = {0};
  t.test_cond_type = S_NSSAI_TEST_COND_TYPE;
  t.S_NSSAI = TRUE_TEST_COND_TYPE;
  t.test_cond = xcalloc(1, sizeof(test_cond_e));
  *t.test_cond = EQUAL_TEST_COND;
  t.test_cond_value = xcalloc(1, sizeof(test_cond_value_t));
  t.test_cond_value->type = OCTET_STRING_TEST_COND_VALUE;
  t.test_cond_value->octet_string_value = xcalloc(1, sizeof(byte_array_t));

  uint8_t oct[4];
  const size_t len = skm_snssai_octets(snssai, oct);
  t.test_cond_value->octet_string_value->len = len;
  t.test_cond_value->octet_string_value->buf = xcalloc(len, 1);
  memcpy(t.test_cond_value->octet_string_value->buf, oct, len);
  return t;
}

/// REPORT estilo 4 para um slice, só com as medidas da xApp que o nó oferece
static bool slice_subscription(const ric_report_style_item_t *style, skm_snssai_t snssai, kpm_sub_data_t *sub)
{
  size_t n_meas = 0;
  for (size_t i = 0; i < style->meas_info_for_action_lst_len; i++) {
    char *name = cp_ba_to_str(style->meas_info_for_action_lst[i].name);
    n_meas += skm_meas_from_name(name) >= 0;
    free(name);
  }
  if (n_meas == 0)
    return false;

  memset(sub, 0, sizeof(*sub));
  sub->ev_trg_def.type = FORMAT_1_RIC_EVENT_TRIGGER;
  sub->ev_trg_def.kpm_ric_event_trigger_format_1.report_period_ms = period_ms;
  sub->sz_ad = 1;
  sub->ad = xcalloc(1, sizeof(kpm_act_def_t));

  kpm_act_def_t *ad = sub->ad;
  ad->type = FORMAT_4_ACTION_DEFINITION;
  ad->frm_4.matching_cond_lst_len = 1;
  ad->frm_4.matching_cond_lst = xcalloc(1, sizeof(matching_condition_format_4_lst_t));
  ad->frm_4.matching_cond_lst[0].test_info_lst = snssai_condition(snssai);

  kpm_act_def_format_1_t *f1 = &ad->frm_4.action_def_format_1;
  f1->meas_info_lst_len = n_meas;
  f1->meas_info_lst = xcalloc(n_meas, sizeof(meas_info_format_1_lst_t));
  size_t k = 0;
  for (size_t i = 0; i < style->meas_info_for_action_lst_len; i++) {
    char *name = cp_ba_to_str(style->meas_info_for_action_lst[i].name);
    const bool wanted = skm_meas_from_name(name) >= 0;
    free(name);
    if (!wanted)
      continue;
    meas_info_format_1_lst_t *item = &f1->meas_info_lst[k++];
    item->meas_type.type = NAME_MEAS_TYPE;
    item->meas_type.name = copy_byte_array(style->meas_info_for_action_lst[i].name);
    item->label_info_lst_len = 1;
    item->label_info_lst = xcalloc(1, sizeof(label_info_lst_t));
    item->label_info_lst[0].noLabel = xcalloc(1, sizeof(enum_value_e));
    *item->label_info_lst[0].noLabel = TRUE_ENUM_VALUE;
  }
  f1->gran_period_ms = period_ms;
  f1->cell_global_id = NULL;
#if defined KPM_V2_03 || defined KPM_V3_00
  f1->meas_bin_range_info_lst_len = 0;
  f1->meas_bin_info_lst = NULL;
#endif
  return true;
}

/* ---- Argumentos e main -------------------------------------------------- */

static _Noreturn void usage(const char *prog)
{
  fprintf(stderr,
          "uso: %s --slice SST.SD [--slice ...] [--json ARQ] [--per-ue] [--period-ms N] [opções do FlexRIC]\n",
          prog);
  exit(2);
}

/// lê as opções da xApp e deixa as outras em fr_argv, para o FlexRIC
static int parse_args(int argc, char **argv, char **fr_argv)
{
  int fr_argc = 0;
  fr_argv[fr_argc++] = argv[0];
  for (int i = 1; i < argc; i++) {
    const bool has_val = i + 1 < argc;
    if (strcmp(argv[i], "--slice") == 0 && has_val) {
      skm_snssai_t s;
      if (n_slices >= SKM_MAX_SLICES || !skm_parse_snssai(argv[++i], &s)) {
        fprintf(stderr, "slice inválido ou slices demais: %s\n", argv[i]);
        usage(argv[0]);
      }
      skm_slice_init(&slices[n_slices++], s);
    } else if (strcmp(argv[i], "--json") == 0 && has_val) {
      json_out = fopen(argv[++i], "a");
      if (json_out == NULL) {
        fprintf(stderr, "não consigo abrir %s: %s\n", argv[i], strerror(errno));
        exit(1);
      }
    } else if (strcmp(argv[i], "--per-ue") == 0) {
      per_ue = true;
    } else if (strcmp(argv[i], "--period-ms") == 0 && has_val) {
      const long v = strtol(argv[++i], NULL, 10);
      if (v < 10 || v > 60000)
        usage(argv[0]);
      period_ms = (uint32_t)v;
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      usage(argv[0]);
    } else {
      fr_argv[fr_argc++] = argv[i];
    }
  }
  if (n_slices == 0)
    usage(argv[0]);
  return fr_argc;
}

static const ric_report_style_item_t *find_style_4(const e2_node_connected_xapp_t *node)
{
  for (size_t f = 0; f < node->len_rf; f++) {
    const sm_ran_function_t *rf = &node->rf[f];
    if (rf->id != SKM_KPM_RAN_FUNC_ID || rf->defn.type != KPM_RAN_FUNC_DEF_E)
      continue;
    for (size_t s = 0; s < rf->defn.kpm.sz_ric_report_style_list; s++) {
      const ric_report_style_item_t *style = &rf->defn.kpm.ric_report_style_list[s];
      if (style->report_style_type == STYLE_4_RIC_SERVICE_REPORT
          && style->act_def_format_type == FORMAT_4_ACTION_DEFINITION)
        return style;
    }
  }
  return NULL;
}

int main(int argc, char **argv)
{
  char **fr_argv = xcalloc((size_t)argc + 1, sizeof(char *));
  const int fr_argc = parse_args(argc, argv, fr_argv);

  fr_args_t args = init_fr_args(fr_argc, fr_argv);
  init_xapp_api(&args);
  sleep(1);

  e2_node_arr_xapp_t nodes = e2_nodes_xapp_api();
  if (nodes.len == 0) {
    fprintf(stderr, "nenhum E2 node conectado ao RIC\n");
    return 1;
  }
  printf("E2 nodes conectados: %d; slices: %zu; período %u ms\n", nodes.len, n_slices, period_ms);

  int handles[SKM_MAX_SLICES * 4];
  size_t n_handles = 0;
  for (size_t i = 0; i < (size_t)nodes.len; i++) {
    e2_node_connected_xapp_t *node = &nodes.n[i];
    const ric_report_style_item_t *style = find_style_4(node);
    if (style == NULL) {
      printf("E2 node %zu: sem KPM REPORT estilo 4, ignorado\n", i);
      continue;
    }
    for (size_t s = 0; s < n_slices && n_handles < sizeof(handles) / sizeof(handles[0]); s++) {
      kpm_sub_data_t sub;
      if (!slice_subscription(style, slices[s].snssai, &sub)) {
        printf("E2 node %zu: nenhuma das medidas da xApp no estilo 4, ignorado\n", i);
        break;
      }
      sm_ans_xapp_t ans = report_sm_xapp_api(&node->id, SKM_KPM_RAN_FUNC_ID, &sub, slice_cb[s]);
      free_kpm_sub_data(&sub);
      char name[16];
      skm_format_snssai(slices[s].snssai, name, sizeof(name));
      if (!ans.success) {
        fprintf(stderr, "E2 node %zu: assinatura do slice %s recusada\n", i, name);
        continue;
      }
      printf("E2 node %zu: assinado o slice %s\n", i, name);
      handles[n_handles++] = ans.u.handle;
    }
  }
  if (n_handles == 0) {
    fprintf(stderr, "nenhuma assinatura aceita\n");
    free_e2_node_arr_xapp(&nodes);
    return 1;
  }

  xapp_wait_end_api();

  for (size_t h = 0; h < n_handles; h++)
    rm_report_sm_xapp_api(handles[h]);
  free_e2_node_arr_xapp(&nodes);
  while (try_stop_xapp_api() == false)
    usleep(1000);
  if (json_out != NULL)
    fclose(json_out);
  free(fr_argv);
  printf("slice_kpm_monitor encerrada\n");
  return 0;
}
