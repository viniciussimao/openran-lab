/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * skm_core: lógica da xApp slice_kpm_monitor, sem dependência de RIC.
 *
 * Recebe as medidas KPM por UE de um relatório de um slice, descarta o
 * primeiro relatório de cada UE (o agente E2 do OAI calcula as medidas como
 * diferença de contadores, e o primeiro relatório parte de zero), soma as
 * medidas do slice e formata o resultado como texto e como JSON Lines.
 *
 * O adaptador de cada Near-RT RIC (hoje, o FlexRIC em skm_flexric.c) só
 * converte as indicações para skm_ue_sample_t e chama skm_aggregate().
 */

#ifndef SKM_CORE_H
#define SKM_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SKM_SD_NONE 0xffffffu ///< S-NSSAI sem SD
#define SKM_MAX_UES 64 ///< UEs por slice acompanhados entre relatórios

/// S-NSSAI (3GPP TS 23.003): SST de 8 bits e SD de 24 bits
typedef struct {
  uint8_t sst;
  uint32_t sd;
} skm_snssai_t;

/** @brief Lê uma S-NSSAI no formato "SST.SD" com SD hexadecimal de 6
 *         dígitos (por exemplo, "1.000001") ou só "SST" (SD = SKM_SD_NONE).
 *  @return true se o texto é válido */
bool skm_parse_snssai(const char *text, skm_snssai_t *out);

/** @brief Escreve a S-NSSAI como "SST.SD" (ou "SST" sem SD). */
void skm_format_snssai(skm_snssai_t snssai, char *buf, size_t len);

/** @brief S-NSSAI em 4 octetos (SST, SD), como na condição de teste do
 *         E2SM-KPM. Sem SD, só o primeiro octeto vale.
 *  @return número de octetos usados (1 ou 4) */
size_t skm_snssai_octets(skm_snssai_t snssai, uint8_t out[4]);

/// Medidas acompanhadas pela xApp (nomes da 3GPP TS 28.552)
typedef enum {
  SKM_PRB_DL, ///< RRU.PrbTotDl: % dos PRBs DL da célula usados pelo UE
  SKM_PRB_UL, ///< RRU.PrbTotUl: % dos PRBs UL da célula usados pelo UE
  SKM_THP_DL, ///< DRB.UEThpDl: vazão DL do UE, kbit/s
  SKM_THP_UL, ///< DRB.UEThpUl: vazão UL do UE, kbit/s
  SKM_N_MEAS,
} skm_meas_t;

/** @brief Nome 3GPP da medida. */
const char *skm_meas_name(skm_meas_t meas);

/** @brief Medida pelo nome 3GPP.
 *  @return a medida, ou -1 se a xApp não usa esse nome */
int skm_meas_from_name(const char *name);

/// Medidas de um UE num relatório
typedef struct {
  uint64_t ue_key; ///< identificador do UE no E2 (por exemplo, RAN UE ID)
  bool has[SKM_N_MEAS];
  double value[SKM_N_MEAS];
} skm_ue_sample_t;

/// Estado de um slice entre relatórios
typedef struct {
  skm_snssai_t snssai;
  size_t n_known;
  uint64_t known[SKM_MAX_UES]; ///< UEs presentes no relatório anterior
} skm_slice_t;

/// Resultado de um relatório de um slice
typedef struct {
  int64_t ts_us; ///< início da coleta, em µs desde a época Unix
  skm_snssai_t snssai;
  int ues; ///< UEs somados
  int new_ues; ///< UEs no primeiro relatório, descartados
  bool has[SKM_N_MEAS]; ///< algum UE somado trouxe a medida
  double sum[SKM_N_MEAS];
} skm_slice_report_t;

/** @brief Inicia o estado de um slice. */
void skm_slice_init(skm_slice_t *slice, skm_snssai_t snssai);

/** @brief Soma as medidas dos UEs de um relatório do slice. Um UE que não
 *         estava no relatório anterior conta em new_ues e fica de fora da
 *         soma. Os UEs deste relatório passam a ser os conhecidos.
 *  @param ts_us início da coleta, do cabeçalho da indicação */
skm_slice_report_t skm_aggregate(skm_slice_t *slice, int64_t ts_us, const skm_ue_sample_t *ues, size_t n);

/** @brief Uma linha de texto legível, sem quebra de linha no fim.
 *  @return o que snprintf retorna */
int skm_report_text(const skm_slice_report_t *report, char *buf, size_t len);

/** @brief Um objeto JSON numa linha, sem quebra de linha no fim. Medidas
 *         sem valor ficam de fora.
 *  @return o que snprintf retorna */
int skm_report_json(const skm_slice_report_t *report, char *buf, size_t len);

#endif /* SKM_CORE_H */
