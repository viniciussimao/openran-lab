/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * sqc_core: lógica da xApp slice_quota_ctrl, sem dependência de RIC.
 *
 * Lê as cotas de PRB por slice da linha de comando ("SST.SD:min:max:ded"),
 * confere as regras do RRMPolicyRatio (3GPP TS 28.541) com as mesmas
 * condições do escalonador do gNB e codifica o PLMN e o SD nos octetos que
 * a ação "Slice-level PRB quota" do E2SM-RC (v1.03, 8.4.3.6) usa.
 *
 * O adaptador de cada Near-RT RIC (hoje, o FlexRIC em sqc_flexric.c) só
 * monta a mensagem de CONTROL com esses valores e a envia.
 */

#ifndef SQC_CORE_H
#define SQC_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SQC_SD_NONE 0xffffffu ///< S-NSSAI sem SD
#define SQC_MAX_QUOTAS 8 ///< mesmo limite do escalonador do gNB

/// Cota de PRB de um slice, em porcentagem: 0 <= dedicated <= min <= max <= 100
typedef struct {
  uint8_t sst;
  uint32_t sd; ///< SQC_SD_NONE quando o slice não tem SD
  uint8_t min_ratio;
  uint8_t max_ratio;
  uint8_t dedicated_ratio;
} sqc_quota_t;

/// PLMN (MCC de 3 dígitos, MNC de 2 ou 3)
typedef struct {
  uint16_t mcc;
  uint16_t mnc;
  uint8_t mnc_digit_len;
} sqc_plmn_t;

/** @brief Lê uma cota no formato "SST.SD:min:max:ded", com SD hexadecimal de
 *         6 dígitos (por exemplo, "1.000001:0:70:0"), ou "SST:min:max:ded".
 *  @return true se o texto é válido e 0 <= ded <= min <= max <= 100 */
bool sqc_parse_quota(const char *text, sqc_quota_t *out);

/** @brief Lê um PLMN no formato "MCC.MNC" (por exemplo, "001.01").
 *  @return true se o texto é válido */
bool sqc_parse_plmn(const char *text, sqc_plmn_t *out);

/** @brief PLMN Identity em 3 octetos BCD (3GPP TS 38.413, 9.3.3.5). */
void sqc_plmn_octets(sqc_plmn_t plmn, uint8_t out[3]);

/** @brief SD em 3 octetos, do mais significativo ao menos. */
void sqc_sd_octets(uint32_t sd, uint8_t out[3]);

/** @brief Confere uma lista de cotas com as regras do gNB: no máximo
 *         SQC_MAX_QUOTAS, sem S-NSSAI repetida, cada cota com
 *         ded <= min <= max <= 100 e soma dos min até 100.
 *  @return true se a lista é válida; senão, o motivo vai para err */
bool sqc_validate(const sqc_quota_t *quotas, size_t n, char *err, size_t err_len);

/** @brief Escreve a cota como "1.000001 min 0 max 70 ded 0".
 *  @return o que snprintf devolve */
int sqc_format_quota(const sqc_quota_t *q, char *buf, size_t len);

#endif /* SQC_CORE_H */
