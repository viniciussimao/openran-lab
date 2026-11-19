# Etapa 2: multi-slice

**Status:** em andamento. A investigação do controle de slice (abaixo) está feita, só por leitura de código; os testes no servidor ainda não foram feitos.

**Objetivo:** dois slices (S-NSSAI) no core e no gNB, um UE em cada, e uma xApp que altera a partição de recursos entre eles.

**Critério de aceite:** mudar a cota de um slice muda a vazão medida com `iperf3` (veja o [roadmap](../roadmap.md)).

## Investigação: controle de slice

**Pergunta:** qual service model permite controlar a partição de PRBs entre slices no OAI `2026.w46`?

**Resposta curta:** nenhum, sem mudar o código do OAI. Nenhum dos três caminhos anunciados no E2 Setup (E2SM-RC, E2SM-CCC e SLICE) chega ao escalonador MAC, e o escalonador MAC do gNB não tem escalonamento por slice. A proposta é escrever um patch próprio para o OAI: uma política de alocação de PRBs por slice no MAC, controlada pelo E2SM-RC (CONTROL estilo 2, "Slice-level PRB quota"). Veja a [decisão 20](../decisions.md#20-controle-de-slice-por-e2sm-rc-com-patch-próprio-no-oai-proposta).

### Método

Leitura do código do OAI na tag `2026.w46` (commit `cb98db5b`) e do submódulo do FlexRIC (commit `18c33250`), clonados fora do repositório com:

```bash
git clone --depth 1 --branch 2026.w46 --recurse-submodules --shallow-submodules \
  https://github.com/duranta-project/openairinterface5g.git
```

Os caminhos abaixo são relativos à raiz do OAI; os do FlexRIC começam em `openair2/E2AP/flexric/`. Os números de linha valem para essa tag.

### 1. Agente E2 do OAI: o que o CONTROL faz de fato

As funções de controle do agente ficam na tabela `write_ctrl_tbl`, preenchida em `init_write_ctrl()` (`openair2/E2AP/RAN_FUNCTION/init_ran_func.c:60`). Ela tem entradas para MAC, RLC, SLICE, TC, GTP, PDCP e RC. **Não há entrada para CCC.**

| Service model | CONTROL anunciado | O que o código faz | Chega ao MAC? |
| --- | --- | --- | --- |
| E2SM-RC (ID 3) | Estilo 1, ação 2 "QoS flow mapping configuration"; estilo 3, ação 1 "Handover Control" (`fill_rc_control()`, `O-RAN/ran_func_rc.c:556`) | `write_ctrl_rc_sm()` (`ran_func_rc.c:1372`) despacha por estilo. O estilo 1 (`write_ctrl_radio_bearer()`, linha 1217) só valida os parâmetros com `assert` e faz `printf`. O estilo 3 (`write_ctrl_conn_mode_mobility()`, linha 1290) dispara de fato um handover F1 ou N2 pelo RRC, só no CU-CP. Qualquer outro estilo é recusado com `RC Control Style %d not supported` | Não |
| E2SM-CCC (ID 4) | Anunciado pelo plugin do FlexRIC, com definição JSON fixa (`on_e2_setup_ccc_sm_ag()`, `flexric/src/sm/ccc_sm/ccc_sm_agent.c:280`) | Como o OAI não registra `CCC_CTRL_REQ_V0`, `io.write_ctrl` fica `NULL` (`ccc_sm_agent.c:461`). O plugin imprime a mensagem e responde **sucesso com listas vazias** (`ccc_sm_agent.c:246`), sem aplicar nada. O `CMakeLists.txt` do E2AP diz: "The CCC SM is not yet supported in OAI RAN" | Não |
| SLICE (ID 145) | Anunciado pelo plugin do FlexRIC | `write_ctrl_slice_sm()` (`CUSTOMIZED/ran_func_slice.c:29`) só imprime `SLICE CONTROL ADD/DEL/ASSOC rx`. `read_slice_sm()` (linha 10) preenche a indicação com **dados aleatórios** (`fill_slice_ind_data()`, de `flexric/test/rnd/`). O README do E2AP confirma: SLICE não é suportado no OAI RAN | Não |
| MAC (custom) | Anunciado pelo plugin do FlexRIC | `write_ctrl_mac_sm()` (`CUSTOMIZED/ran_func_mac.c:106`) imprime `operation not supported` | Não |

Por que o CCC e o SLICE aparecem no E2 Setup: o agente carrega como plugin todo arquivo da pasta de service models (`load_all_pugin_ag()`, `flexric/src/agent/plugin_agent.c:140`) e anuncia todos. Estar no E2 Setup **não** significa estar implementado no OAI.

Outras observações:

- O README do E2AP na tag `2026.w46` lista, além do que já sabíamos da `2026.w28`, o REPORT estilo 5 e o CONTROL estilo 3 (handover). Nenhum estilo de controle de recursos de rádio.
- `grep` por `RRMPolicy`, `prb_quota`, `slice level` e `E2SM-CCC` no OAI (fora do FlexRIC) não encontra nada.
- Cuidado: no estilo 1 do RC, os parâmetros são conferidos com `assert`. Uma xApp que mande outra ação do estilo 1 (por exemplo, a `xapp_keysight_kpm_rc`, que manda a ação 1 "DRB QoS Configuration") derruba o gNB (`ran_func_rc.c:1219`).
- Demo com a VIAVI (newsletter de inverno 2025): não há código correspondente na tag nem branch público com nome relacionado (`git ls-remote --heads` mostra só `qoe-e2` e `e2ap-node-config` ligados ao E2). Se o código existe, não está publicado no repositório oficial.

### 2. Escalonador MAC do gNB e slices

O escalonador NR foi reorganizado num pipeline com funções trocáveis por ponteiro, declaradas em `openair2/LAYER2/NR_MAC_gNB/nr_mac_gNB.h`: `dl_ri_pmi_select`, `dl_tda_select`, `dl_beam_select`, `dl_mcs_select`, `dl_rb_alloc` e `dl_lcid_alloc`, e as equivalentes de UL. A alocação de PRBs é a `dl_rb_alloc` / `ul_rb_alloc` (`nr_dl_rb_alloc_fn`, linha 1077; `nr_ul_rb_alloc_fn`, linha 1208).

- **Algoritmo:** proportional fair, sem noção de slice. `main.c:283` e `main.c:297` fixam `nr_ul_proportional_fair` e `nr_dl_proportional_fair` (`gNB_scheduler_*_default_policies.c`). Não há opção de configuração, plugin nem comando para trocar a política; o único outro lugar que atribui `dl_rb_alloc` é o benchmark `tests/nrdlbench/dlbench.c`.
- **Informação de slice disponível:** cada candidato a escalonamento já traz a S-NSSAI, o 5QI e a prioridade do primeiro DRB (`nr_dl_candidate_t.nssai`, `nr_mac_gNB.h:1011`; `nr_ul_candidate_t.nssai`, linha 1132), preenchidos em `gNB_scheduler_dlsch.c:472` e `gNB_scheduler_ulsch.c:2767` com o comentário "for external policies". A política padrão não usa esses campos.
- **Parâmetros de partição (mínima, máxima, dedicada):** não existem no MAC NR. O módulo `openair2/LAYER2/MAC/slicing/` (estático e NVS) é do eNB LTE e não é usado pelo gNB.
- **Configuração:**
  - arquivo do gNB: só a lista `snssaiList` (`sst`, `sd`) da célula (`openair2/GNB_APP/gnb_paramdef.h:579`), sem cotas;
  - E2: nenhum caminho chega ao MAC (seção 1);
  - O1/telnet: `telnetsrv_o1.c:396` tem `stats`, `config`, `bwconfig`, `stop_modem`, `start_modem` e `remove_mac_ues`. O `bwconfig` muda o BWP da célula inteira e exige parar o modem; nada por slice.

### 3. xApps de controle no FlexRIC do submódulo

| xApp (binário) | Service model e ação | Compatível com o OAI `2026.w46`? |
| --- | --- | --- |
| `xapp_kpm_rc` | RC, estilo 1, ação 2 (QoS flow mapping) | Aceita, mas só gera `printf` no gNB (o README do E2AP diz o mesmo) |
| `xapp_kpm_rc_Enhanced` | RC, estilo 1, ação 2 | Igual à anterior |
| `xapp_keysight_kpm_rc` | RC, estilo 1, ação 1 (DRB QoS Configuration) | **Não**: o `assert` da ação 2 derruba o gNB |
| `xapp_rc_handover` | RC, estilo 3, ação 1 (Handover Control) | Sim, no CU-CP (não muda PRBs) |
| `xapp_es_with_cell_util` | RC, estilo próprio 300 (energy state) | Não: estilo recusado |
| `xapp_test_ccc` | CCC, estilo 1 (`O-RRMPolicyRatio` com `rRMPolicyMaxRatio`, `MinRatio` e `DedicatedRatio`) e estilo 2 (`O-Bwp`, `O-NESPolicy`...) | Não: o gNB responde sucesso com listas vazias e nada muda. O formato da mensagem é o que precisaríamos |
| `xapp_rf_reconfiguration` | CCC, estilo 2 | Não, pelo mesmo motivo |
| `xapp_slice_moni_ctrl` (C e Python) | SLICE: adiciona slices estáticos (`pos_low`/`pos_high`), NVS ou EDF; associa UE a slice | Não: o gNB só imprime, e a monitoração devolve dados aleatórios. O próprio código diz que NVS/EDF só existiam no eNB |
| `xapp_mac_ctrl` | MAC (custom) | Não: `operation not supported` |

Os emuladores de agente do FlexRIC (`examples/emulator/agent/sm_slice.c`, `sm_ccc.c`, `sm_rc.c`) aceitam esses controles, mas também não aplicam nada a um escalonador real.

### 4. Recomendação

**Caminho proposto:** patch próprio para o OAI, aplicado no provisionamento, com duas partes:

1. **MAC:** uma política `dl_rb_alloc` (e depois `ul_rb_alloc`) que agrupa os candidatos pela `nssai` e reparte os PRBs por slice com cotas mínima, máxima e dedicada (o modelo `RRMPolicyRatio` da 3GPP TS 28.541), chamando o proportional fair dentro de cada slice. O pipeline e os campos `nssai` dos candidatos já existem, então a mudança fica isolada num arquivo novo mais o registro em `main.c`.
2. **E2:** o E2SM-RC passa a anunciar e tratar o CONTROL estilo 2 ("Radio Resource Allocation Control"), ação "Slice-level PRB quota" (E2SM-RC v1.03, seção 7.6.3), com a lista de políticas por S-NSSAI, e a escrever as cotas numa estrutura lida pela política do MAC.

**Por que o E2SM-RC e não o CCC:** os dois são padrão O-RAN e os dois exigem o mesmo trabalho no MAC; muda só o adaptador E2. O RC segue a decisão 18: é o service model que o Near-RT RIC da O-RAN SC e o xDevSM usam (Etapa 8), e o OAI já tem o despachante do RC pronto. O CCC fica como alternativa: o FlexRIC já traz a mensagem `O-RRMPolicyRatio` e a `xapp_test_ccc`, e o ORANSlice seguiu esse caminho. O SLICE SM está descartado: também exigiria implementar tudo no OAI, e não é padrão.

**Grau de confiança:**

- **Alto** de que o OAI `2026.w46` não tem controle de PRB por slice por nenhum caminho: o código foi lido do agente E2 até o MAC.
- **Médio** quanto ao esforço do patch: a estrutura do escalonador ajuda, mas a política precisa conviver com retransmissões HARQ, feixes e CCE.
- **Médio** de que a biblioteca RC do FlexRIC codifica o estilo 2 sem mudanças: ela trata estilos e parâmetros RAN de forma genérica por ID, mas não tem constantes para "Slice-level PRB quota", e o caminho não foi exercitado.

### 5. A confirmar no servidor

Nada disso muda a infraestrutura. Tudo roda na VM `ran` com o cenário da Etapa 1 no ar (core, nearRT-RIC, gNB e UE, como no passo 5 do [guia da Etapa 1](01-ran-core-e2.md)), com duas diferenças:

- antes de subir o gNB, inicie a captura do E2AP (item 4);
- suba o gNB com `| tee /tmp/gnb.log` no fim da linha do `nr-softmodem`, para guardar o log.

As xApps rodam num quinto terminal, a partir de `~/openairinterface5g/openair2/E2AP/flexric`. Depois de cada uma, procure a linha esperada no log do gNB.

1. **SLICE é só stub.**

   ```bash
   XAPP_DURATION=20 ./build/examples/xApp/c/slice/xapp_slice_moni_ctrl
   grep -E "SLICE CONTROL (ADD|DEL|ASSOC) rx" /tmp/gnb.log
   ```

   Esperado: a linha `SLICE CONTROL ADD rx` no gNB e nenhuma outra reação. Os valores que a xApp imprime na monitoração são aleatórios.

2. **CCC responde sem aplicar.**

   ```bash
   ./build/examples/xApp/c/orange/xapp_test_ccc
   grep -n "CCC SM Agent" /tmp/gnb.log
   ```

   Esperado: o TC-6 (`O-RRMPolicyRatio`) aparece como enviado na xApp, e o gNB mostra só `[CCC SM Agent]: Control message received`.

3. **RC estilo 1 é só `printf`.**

   ```bash
   XAPP_DURATION=20 ./build/examples/xApp/c/kpm_rc/xapp_kpm_rc
   grep -n -A3 "QoS flow mapping configuration" /tmp/gnb.log
   ```

   Esperado: `QoS flow mapping configuration`, `DRB ID` e `qfi` no log do gNB, sem efeito no UE. **Não** rode a `xapp_keysight_kpm_rc`: ela derruba o gNB.

4. **Definição do RC no E2 Setup.** Antes de subir o gNB, num terminal à parte:

   ```bash
   sudo tcpdump -i lo -w /tmp/e2setup.pcap sctp port 36421
   ```

   Pare com Ctrl+C depois do E2 Setup, copie `/tmp/e2setup.pcap` para o notebook e abra no Wireshark. Na RAN Function Definition do RC (ID 3), confira que só há CONTROL estilos 1 e 3, e que a do CCC (ID 4) é o JSON fixo `"ranFunctionName":"CCC"`.

5. **Demo VIAVI.** Perguntar à OAI (lista de discussão ou issue) se o código do controle de slice por E2SM-RC mostrado com a VIAVI será publicado. Se for, a decisão 20 pode mudar para usá-lo.

Cole as saídas dos itens 1 a 4 no PR. Depois dos testes, atualize esta seção e o status da decisão 20.
