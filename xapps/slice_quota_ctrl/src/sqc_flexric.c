/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * slice_quota_ctrl: xApp que muda as cotas de PRB por slice, adaptador FlexRIC.
 *
 * Procura, em cada E2 node, o CONTROL estilo 2 do E2SM-RC ("Radio Resource
 * Allocation Control") com a ação 6 ("Slice-level PRB quota", E2SM-RC v1.03,
 * 8.4.3.6) e envia um CONTROL com as cotas pedidas. O gNB atualiza só os
 * slices da mensagem e mantém os outros. A cota neutra (min 0, max 100,
 * ded 0, o que --remove envia) remove a cota do slice; sem nenhuma cota, o
 * gNB volta à alocação padrão. Não há como remover todas de uma vez: uma
 * LIST do E2SM-RC precisa de ao menos um item.
 *
 * Uso:
 *   slice_quota_ctrl [--quota SST.SD:min:max:ded ...] [--remove SST.SD ...]
 *                    [--plmn MCC.MNC] [opções do FlexRIC]
 *   slice_quota_ctrl --check [opções do FlexRIC]
 *
 *   --quota Q   cota de um slice, por exemplo 1.000001:0:70:0 (min, max e
 *               dedicated, em % dos PRBs); repita para cada slice
 *   --remove S  remove a cota do slice S ("SST.SD" ou "SST"); pode se repetir
 *               e se misturar com --quota
 *   --plmn P    PLMN dos slices (padrão 001.01)
 *   --check     só mostra o que cada E2 node anuncia no estilo 2, sem CONTROL
 *   Outras opções (por exemplo, -c arquivo.conf) vão para o FlexRIC.
 *
 * O agente E2 do OAI responde CONTROL ACKNOWLEDGE mesmo quando recusa as
 * cotas (o agente do FlexRIC não envia CONTROL FAILURE): o resultado fica no
 * log do gNB, nas linhas [SLICE].
 */

#include "sqc_core.h"

#include "xApp/e42_xapp_api.h"
#include "sm/rc_sm/ie/rc_data_ie.h"
#include "sm/rc_sm/ie/ir/lst_ran_param.h"
#include "sm/rc_sm/ie/ir/ran_param_list.h"
#include "sm/rc_sm/ie/ir/ran_param_struct.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* RAN function ID do E2SM-RC no FlexRIC (sm/rc_sm/rc_sm_id.h define o símbolo
 * SM_RC_ID como variável global; repetido aqui para não duplicá-lo) */
#define SQC_RC_RAN_FUNC_ID 3
#define SQC_RC_CTRL_STYLE 2
#define SQC_RC_CTRL_ACTION 6

/* IDs dos parâmetros da ação 6 (8.4.3.6). Nos valores, os itens de LIST (2 e
 * 5) não levam ID: cada item é direto a STRUCTURE com os membros dele. */
enum {
  RRM_POLICY_RATIO_LIST = 1,
  RRM_POLICY = 3,
  RRM_POLICY_MEMBER_LIST = 4,
  PLMN_IDENTITY = 6,
  S_NSSAI = 7,
  SST = 8,
  SD = 9,
  MIN_PRB_POLICY_RATIO = 10,
  MAX_PRB_POLICY_RATIO = 11,
  DEDICATED_PRB_POLICY_RATIO = 12,
};

static sqc_quota_t quotas[SQC_MAX_QUOTAS];
static size_t n_quotas;
static bool check_only;
static sqc_plmn_t plmn = {.mcc = 1, .mnc = 1, .mnc_digit_len = 2};

static void *xcalloc(size_t n, size_t sz)
{
  void *p = calloc(n, sz);
  assert(p != NULL && "memória esgotada");
  return p;
}

/* ---- Montagem da mensagem ----------------------------------------------- */

static seq_ran_param_t elem_octets(uint32_t id, const uint8_t *buf, size_t len)
{
  seq_ran_param_t p = {.ran_param_id = id};
  p.ran_param_val.type = ELEMENT_KEY_FLAG_FALSE_RAN_PARAMETER_VAL_TYPE;
  p.ran_param_val.flag_false = xcalloc(1, sizeof(ran_parameter_value_t));
  p.ran_param_val.flag_false->type = OCTET_STRING_RAN_PARAMETER_VALUE;
  p.ran_param_val.flag_false->octet_str_ran.len = len;
  p.ran_param_val.flag_false->octet_str_ran.buf = xcalloc(len, 1);
  memcpy(p.ran_param_val.flag_false->octet_str_ran.buf, buf, len);
  return p;
}

static seq_ran_param_t elem_int(uint32_t id, int64_t v)
{
  seq_ran_param_t p = {.ran_param_id = id};
  p.ran_param_val.type = ELEMENT_KEY_FLAG_FALSE_RAN_PARAMETER_VAL_TYPE;
  p.ran_param_val.flag_false = xcalloc(1, sizeof(ran_parameter_value_t));
  p.ran_param_val.flag_false->type = INTEGER_RAN_PARAMETER_VALUE;
  p.ran_param_val.flag_false->int_ran = v;
  return p;
}

/// STRUCTURE com n membros, para o chamador preencher
static seq_ran_param_t *strct(seq_ran_param_t *p, uint32_t id, size_t n)
{
  p->ran_param_id = id;
  p->ran_param_val.type = STRUCTURE_RAN_PARAMETER_VAL_TYPE;
  p->ran_param_val.strct = xcalloc(1, sizeof(ran_param_struct_t));
  p->ran_param_val.strct->sz_ran_param_struct = n;
  p->ran_param_val.strct->ran_param_struct = xcalloc(n, sizeof(seq_ran_param_t));
  return p->ran_param_val.strct->ran_param_struct;
}

/// LIST com n itens, para o chamador preencher
static lst_ran_param_t *lst(seq_ran_param_t *p, uint32_t id, size_t n)
{
  p->ran_param_id = id;
  p->ran_param_val.type = LIST_RAN_PARAMETER_VAL_TYPE;
  p->ran_param_val.lst = xcalloc(1, sizeof(ran_param_list_t));
  p->ran_param_val.lst->sz_lst_ran_param = n;
  p->ran_param_val.lst->lst_ran_param = n > 0 ? xcalloc(n, sizeof(lst_ran_param_t)) : NULL;
  return p->ran_param_val.lst->lst_ran_param;
}

/// membros de um item de LIST
static seq_ran_param_t *item(lst_ran_param_t *it, size_t n)
{
  it->ran_param_struct.sz_ran_param_struct = n;
  it->ran_param_struct.ran_param_struct = xcalloc(n, sizeof(seq_ran_param_t));
  return it->ran_param_struct.ran_param_struct;
}

/// um RRM Policy Ratio Group por cota, com um membro só
static void fill_group(lst_ran_param_t *grp_item, const sqc_quota_t *q)
{
  seq_ran_param_t *grp = item(grp_item, 4);

  seq_ran_param_t *policy = strct(&grp[0], RRM_POLICY, 1);
  lst_ran_param_t *members = lst(&policy[0], RRM_POLICY_MEMBER_LIST, 1);
  seq_ran_param_t *member = item(&members[0], 2);

  uint8_t oct[3];
  sqc_plmn_octets(plmn, oct);
  member[0] = elem_octets(PLMN_IDENTITY, oct, 3);

  const bool has_sd = q->sd != SQC_SD_NONE;
  seq_ran_param_t *snssai = strct(&member[1], S_NSSAI, has_sd ? 2 : 1);
  snssai[0] = elem_octets(SST, &q->sst, 1);
  if (has_sd) {
    sqc_sd_octets(q->sd, oct);
    snssai[1] = elem_octets(SD, oct, 3);
  }

  grp[1] = elem_int(MIN_PRB_POLICY_RATIO, q->min_ratio);
  grp[2] = elem_int(MAX_PRB_POLICY_RATIO, q->max_ratio);
  grp[3] = elem_int(DEDICATED_PRB_POLICY_RATIO, q->dedicated_ratio);
}

static rc_ctrl_req_data_t gen_quota_ctrl(void)
{
  rc_ctrl_req_data_t dst = {0};

  /* CONTROL HEADER formato 1 (9.2.1.6.1). O UE ID é obrigatório, mas a ação 6
   * vale para a célula: vai um UE ID de gNB zerado, que o gNB ignora. */
  dst.hdr.format = FORMAT_1_E2SM_RC_CTRL_HDR;
  dst.hdr.frmt_1.ric_style_type = SQC_RC_CTRL_STYLE;
  dst.hdr.frmt_1.ctrl_act_id = SQC_RC_CTRL_ACTION;
  dst.hdr.frmt_1.ue_id.type = GNB_UE_ID_E2SM;
  dst.hdr.frmt_1.ue_id.gnb.amf_ue_ngap_id = 0;
  dst.hdr.frmt_1.ue_id.gnb.guami.plmn_id = (e2sm_plmn_t){.mcc = plmn.mcc, .mnc = plmn.mnc, .mnc_digit_len = plmn.mnc_digit_len};

  /* CONTROL MESSAGE formato 1 (9.2.1.7.1) */
  dst.msg.format = FORMAT_1_E2SM_RC_CTRL_MSG;
  dst.msg.frmt_1.sz_ran_param = 1;
  dst.msg.frmt_1.ran_param = xcalloc(1, sizeof(seq_ran_param_t));
  lst_ran_param_t *groups = lst(&dst.msg.frmt_1.ran_param[0], RRM_POLICY_RATIO_LIST, n_quotas);
  for (size_t i = 0; i < n_quotas; i++)
    fill_group(&groups[i], &quotas[i]);
  return dst;
}

/* ---- Anúncio do E2 node ------------------------------------------------- */

static void print_def(const ran_param_lst_struct_t *p, int depth)
{
  char *name = cp_ba_to_str(p->ran_param_name);
  const char *kind = p->ran_param_def == NULL                                       ? ""
                     : p->ran_param_def->type == LIST_RAN_PARAMETER_DEF_TYPE      ? " (LIST)"
                     : p->ran_param_def->type == STRUCTURE_RAN_PARAMETER_DEF_TYPE ? " (STRUCTURE)"
                                                                                  : "";
  printf("  %*s%u %s%s\n", depth * 2, "", p->ran_param_id, name, kind);
  free(name);
  if (p->ran_param_def == NULL)
    return;
  const ran_param_type_t *t = p->ran_param_def->type == LIST_RAN_PARAMETER_DEF_TYPE ? p->ran_param_def->lst : p->ran_param_def->strct;
  for (size_t i = 0; t != NULL && i < t->sz_ran_param; i++)
    print_def(&t->ran_param[i], depth + 1);
}

/// ação 6 do estilo 2 no E2SM-RC do nó, ou NULL
static const seq_ctrl_act_2_t *find_quota_action(const e2_node_connected_xapp_t *node)
{
  for (size_t f = 0; f < node->len_rf; f++) {
    const sm_ran_function_t *rf = &node->rf[f];
    if (rf->id != SQC_RC_RAN_FUNC_ID || rf->defn.type != RC_RAN_FUNC_DEF_E || rf->defn.rc.ctrl == NULL)
      continue;
    const ran_func_def_ctrl_t *ctrl = rf->defn.rc.ctrl;
    for (size_t s = 0; s < ctrl->sz_seq_ctrl_style; s++) {
      const seq_ctrl_style_t *style = &ctrl->seq_ctrl_style[s];
      if (style->style_type != SQC_RC_CTRL_STYLE)
        continue;
      for (size_t a = 0; a < style->sz_seq_ctrl_act; a++)
        if (style->seq_ctrl_act[a].id == SQC_RC_CTRL_ACTION)
          return &style->seq_ctrl_act[a];
    }
  }
  return NULL;
}

static void print_action(size_t node_idx, const seq_ctrl_act_2_t *act)
{
  char *name = cp_ba_to_str(act->name);
  printf("E2 node %zu: E2SM-RC CONTROL estilo %d, ação %u (%s), parâmetros:\n", node_idx, SQC_RC_CTRL_STYLE, act->id, name);
  free(name);
  for (size_t i = 0; i < act->sz_seq_assoc_ran_param; i++) {
    const seq_ran_param_3_t *p = &act->assoc_ran_param[i];
    /* mesmo formato de print_def, a partir do nível de cima */
    ran_param_lst_struct_t top = {.ran_param_id = p->id, .ran_param_name = p->name, .ran_param_def = p->def};
    print_def(&top, 0);
  }
}

/* ---- Argumentos e main -------------------------------------------------- */

static _Noreturn void usage(const char *prog)
{
  fprintf(stderr,
          "uso: %s [--quota SST.SD:min:max:ded ...] [--remove SST.SD ...] [--plmn MCC.MNC] [opções do FlexRIC]\n"
          "     %s --check [opções do FlexRIC]\n",
          prog,
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
    if (strcmp(argv[i], "--quota") == 0 && has_val) {
      if (n_quotas >= SQC_MAX_QUOTAS || !sqc_parse_quota(argv[++i], &quotas[n_quotas])) {
        fprintf(stderr, "cota inválida ou cotas demais: %s (formato SST.SD:min:max:ded, com ded <= min <= max <= 100)\n", argv[i]);
        usage(argv[0]);
      }
      n_quotas++;
    } else if (strcmp(argv[i], "--plmn") == 0 && has_val) {
      if (!sqc_parse_plmn(argv[++i], &plmn)) {
        fprintf(stderr, "PLMN inválido: %s (formato MCC.MNC, por exemplo 001.01)\n", argv[i]);
        usage(argv[0]);
      }
    } else if (strcmp(argv[i], "--remove") == 0 && has_val) {
      if (n_quotas >= SQC_MAX_QUOTAS || !sqc_parse_remove(argv[++i], &quotas[n_quotas])) {
        fprintf(stderr, "slice inválido ou slices demais: %s (formato SST.SD, por exemplo 1.000001)\n", argv[i]);
        usage(argv[0]);
      }
      n_quotas++;
    } else if (strcmp(argv[i], "--check") == 0) {
      check_only = true;
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      usage(argv[0]);
    } else {
      fr_argv[fr_argc++] = argv[i];
    }
  }
  const int modes = (n_quotas > 0) + check_only;
  if (modes != 1)
    usage(argv[0]);
  return fr_argc;
}

int main(int argc, char **argv)
{
  char **fr_argv = xcalloc((size_t)argc + 1, sizeof(char *));
  const int fr_argc = parse_args(argc, argv, fr_argv);

  char err[160];
  if (!sqc_validate(quotas, n_quotas, err, sizeof(err))) {
    fprintf(stderr, "cotas recusadas antes do envio: %s\n", err);
    return 2;
  }

  fr_args_t args = init_fr_args(fr_argc, fr_argv);
  init_xapp_api(&args);
  sleep(1);

  e2_node_arr_xapp_t nodes = e2_nodes_xapp_api();
  if (nodes.len == 0) {
    fprintf(stderr, "nenhum E2 node conectado ao RIC\n");
    return 1;
  }

  int sent = 0, failed = 0, without = 0;
  for (size_t i = 0; i < (size_t)nodes.len; i++) {
    e2_node_connected_xapp_t *node = &nodes.n[i];
    const seq_ctrl_act_2_t *act = find_quota_action(node);
    if (act == NULL) {
      printf("E2 node %zu: sem E2SM-RC CONTROL estilo %d, ação %d; ignorado\n", i, SQC_RC_CTRL_STYLE, SQC_RC_CTRL_ACTION);
      without++;
      continue;
    }
    print_action(i, act);
    if (check_only)
      continue;

    for (size_t q = 0; q < n_quotas; q++) {
      char line[64];
      sqc_format_quota(&quotas[q], line, sizeof(line));
      printf("E2 node %zu: cota %s%s\n", i, line, sqc_quota_is_neutral(&quotas[q]) ? " (remove a cota do slice)" : "");
    }
    rc_ctrl_req_data_t ctrl = gen_quota_ctrl();
    const sm_ans_xapp_t ans = control_sm_xapp_api(&node->id, SQC_RC_RAN_FUNC_ID, &ctrl);
    free_rc_ctrl_req_data(&ctrl);
    if (ans.success) {
      printf("E2 node %zu: CONTROL ACKNOWLEDGE recebido; confira as linhas [SLICE] no log do gNB\n", i);
      sent++;
    } else {
      fprintf(stderr, "E2 node %zu: CONTROL sem resposta positiva\n", i);
      failed++;
    }
  }

  free_e2_node_arr_xapp(&nodes);
  while (try_stop_xapp_api() == false)
    usleep(1000);
  free(fr_argv);

  if (check_only)
    return without == nodes.len ? 1 : 0;
  printf("slice_quota_ctrl: %d CONTROL enviado(s), %d sem resposta positiva\n", sent, failed);
  return (sent > 0 && failed == 0) ? 0 : 1;
}
