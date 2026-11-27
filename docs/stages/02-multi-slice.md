# Etapa 2: multi-slice

**Status:** em andamento. A investigação do controle de slice está feita, só por leitura de código. O desenho do escalonador por slice está aprovado, e a linha de base com o OAI upstream (dois slices, dois UEs) aguarda o teste no servidor.

**Objetivo:** dois slices (S-NSSAI) no core e no gNB, um UE em cada, e uma xApp que altera a partição de recursos entre eles.

**Critério de aceite:** mudar a cota de um slice muda a vazão medida com `iperf3` (veja o [roadmap](../roadmap.md)).

## Investigação: controle de slice

**Pergunta:** qual service model permite controlar a partição de PRBs entre slices no OAI `2026.w46`?

**Resposta curta:** nenhum, sem mudar o código do OAI. Nenhum dos três caminhos anunciados no E2 Setup (E2SM-RC, E2SM-CCC e SLICE) chega ao escalonador MAC, e o escalonador MAC do gNB não tem escalonamento por slice. Decidimos escrever um escalonador por slice num fork próprio do OAI, primeiro com cotas fixas no arquivo de configuração do gNB e depois controlado pelo E2SM-RC (CONTROL estilo 2, "Slice-level PRB quota"). Veja a [decisão 20](../decisions.md#20-escalonador-de-slices-num-fork-próprio-do-oai).

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

5. **Demo VIAVI.** Perguntar à OAI (lista de discussão ou issue) se o código do controle de slice por E2SM-RC mostrado com a VIAVI será publicado. Se for, comparar com o fork antes de escrever o handler E2 (decisão 20).

Cole as saídas dos itens 1 a 4 no PR. Depois dos testes, atualize esta seção.

## Escalonador por slice (prova de conceito)

Desenho aprovado em 2026-11-19, conforme a [decisão 20](../decisions.md#20-escalonador-de-slices-num-fork-próprio-do-oai). O código fica no fork `viniciussimao/openairinterface5g`, na branch `slice-scheduler`, a partir da tag `2026.w46`. A PoC cobre só o downlink, e as cotas são fixas no arquivo de configuração do gNB, sem E2.

### Cotas

As cotas seguem o `RRMPolicyRatio` da 3GPP TS 28.541. Cada slice é identificado pela S-NSSAI (`sst` + `sd`) e recebe três valores, em porcentagem dos PRBs do BWP:

| Parâmetro | Significado |
| --- | --- |
| `dedicated_ratio` | PRBs reservados para o slice. Ficam sem uso se o slice não tiver demanda; nenhum outro slice os recebe |
| `min_ratio` | PRBs garantidos ao slice quando ele tem demanda. Inclui os dedicados. A parte que o slice não usar vai para os outros |
| `max_ratio` | Teto do slice, mesmo com PRBs sobrando |

Regras de validação, conferidas na subida do gNB:

- `0 ≤ dedicated_ratio ≤ min_ratio ≤ max_ratio ≤ 100`;
- a soma dos `min_ratio` de todos os slices é no máximo 100;
- no máximo 8 slices, sem S-NSSAI repetida.

Seção opcional dentro de `MACRLCs.[0]` no `.conf` do gNB. Sem ela, o gNB usa o proportional fair do OAI, sem nenhuma mudança:

```
slice_policies = (
  { sst = 1; sd = 0x000001; min_ratio = 0; max_ratio = 30; dedicated_ratio = 0; },
  { sst = 1; sd = 0x000002; min_ratio = 0; max_ratio = 70; dedicated_ratio = 0; }
);
```

### Algoritmo

A cada slot, com N PRBs no BWP:

1. Calcula os orçamentos em PRBs inteiros, arredondando para baixo:
   1. reserva os dedicados de cada slice configurado, tenha ele demanda ou não;
   2. garante o mínimo de cada slice com demanda, limitado à demanda dele;
   3. reparte o resto em partes iguais entre os slices com demanda não atendida (preenchimento até o nível), respeitando o `max_ratio`. A sobra do arredondamento entra nessa etapa.
2. Dentro de cada slice, chama o proportional fair original do OAI (`nr_dl_proportional_fair()`) com o orçamento do slice como limite de PRBs.
3. A ordem em que os slices chamam o proportional fair alterna a cada slot (round-robin), para nenhum slice ser favorecido nos PRBs nem nos CCEs do PDCCH.

A demanda de cada slice é estimada como o proportional fair faz: os PRBs da retransmissão, `nr_find_nb_rb()` para dados novos e 5 PRBs para quem só tem um CE da MAC. UEs cuja S-NSSAI não está na configuração (por exemplo, só com SRB) vão para um grupo padrão, com `min_ratio = 0`, `dedicated_ratio = 0` e `max_ratio = 100`.

As cotas passam por uma API própria (`nr_slice_policy_set()` e `nr_slice_policy_get()`), que valida a lista e troca as cotas de uma vez sob o `sched_lock`. A configuração usa essa API agora; o handler do E2SM-RC (CONTROL estilo 2, ação 6) vai usá-la depois.

O gNB escreve no log, cerca de uma vez por segundo, os PRBs DL usados por slice e por UE, separando transmissões novas e retransmissões, e os PRBs que ficaram sem uso no período.

### Limitações conhecidas

- **Só vale a S-NSSAI do primeiro DRB do UE.** Um UE com sessões PDU em dois slices é contado no slice da primeira.
- **Uma retransmissão pode passar um pouco do orçamento do slice.** A retransmissão HARQ precisa do mesmo número de PRBs da transmissão original.
- **Um slice com orçamento menor que 5 PRBs só atende retransmissões.** O proportional fair do OAI não aloca menos de 5 PRBs para dados novos.
- **As cotas valem por slot, não como média numa janela.** O `RRMPolicyRatio` não define a janela; aqui cada slot respeita as cotas.

### Critérios de aceite da PoC

O tráfego é UDP DL (`-u -R`) com dois servidores no `oai-ext-dn` (portas 5201 e 5202). Cada UE que precisa saturar o slice manda `-b 200M`: com `-b 100M`, um UE sozinho num slice com folga recebe tudo o que pede e não mostra teto nenhum (veja os [resultados da PoC](#resultados-da-poc)). TCP fica como teste extra. A divisão de PRBs é lida do log do escalonador com `slice-prb-share.sh`; a vazão do iperf3 é um dado complementar.

| Teste | Cenário | Critério |
| --- | --- | --- |
| T0 Regressão | Sem `slice_policies`, 1 UE | Ping 5/5, `xapp_kpm_moni` recebendo KPM e vazão igual à do upstream, dentro do ruído |
| T1 Disputa | Os dois saturados, `max_ratio` 30/70 | Divisão de PRBs DL de cerca de 30/70 (±5 p.p.) |
| T2 Conservação | UE do slice 2 ocioso | O slice 1 chega ao `max_ratio` dele, e não além |
| T3 Mínimo | `.conf` com `min_ratio` diferente de 0, sob disputa | O `min_ratio` do slice menor é respeitado |
| T4 Dedicado | `.conf` com `dedicated_ratio` diferente de 0, slice dedicado ocioso | Os PRBs dedicados não vão para o outro slice |
| T5 Estabilidade | 10 min com os dois saturados | Nenhum assert nem queda do gNB |
| T6 E2 | Os dois UEs com tráfego | A `xapp_kpm_moni` continua recebendo o KPM de célula (REPORT estilo 1) e o gNB não cai. É uma regressão do E2 |

O T6 não cobre o KPM por UE. A `xapp_kpm_moni` filtra os UEs pela S-NSSAI `1.ffffff` e não vê os UEs com SD explícito (veja os resultados da linha de base). O KPM por UE e por slice fica para a xApp de monitoração própria, primeira peça da xApp de slicing, feita logo depois da PoC. Ela assina o REPORT estilo 4 uma vez por slice, com a S-NSSAI de 4 octetos. O critério dela é imprimir `RRU.PrbTotDl` e `DRB.UEThpDl` por UE e por slice, batendo com o log do escalonador.

## Linha de base com o OAI upstream

Antes do escalonador, a linha de base valida o core com dois slices, os dois UEs e a CPU da VM, e mede a divisão do proportional fair puro (a comparação sem cotas). Usa o OAI da tag `2026.w46`, sem o fork.

### O que o provisionamento prepara

| Item | Onde | Como |
| --- | --- | --- |
| Slices `1.000001` e `1.000002` | `lab_slices` em `ansible/group_vars/all.yml` | Fonte única para o core e o gNB |
| Core com os dois slices | `~/oai-cn5g/conf/config.lab.yaml` | A role `oai_cn5g` gera o arquivo a partir do `config.yaml` do OAI, que fica intacto, só inserindo os slices na AMF, no SMF (`smf_info` e assinaturas locais, DNN `oai`) e no UPF. O override do compose monta esse arquivo no lugar do original nos sete NFs |
| gNB com os dois slices | `~/gnb-slices.conf` | A role `oai_ran` gera o arquivo a partir do `.conf` da Etapa 1, trocando só a `snssaiList` |
| Um UE por slice | `~/ue-slice1.conf` e `~/ue-slice2.conf` | Copiados de `configs/`. IMSIs `001010000000001` e `001010000000002`, já cadastrados no banco do CN5G com a mesma chave e OPc |
| Namespaces dos UEs | `~/multi-ue-ns.sh` | Copiado de `scripts/`. Cria `ue1` e `ue2`, com o RFsimulator em `10.201.1.100` e `10.202.1.100` |

O banco do core não muda: o SMF usa as assinaturas locais (`use_local_subscription_info: yes`), e os dois IMSIs já existem no `oai_db.sql` do OAI.

### Roteiro

No servidor de teste, atualize o repositório e provisione duas vezes. Na segunda, o esperado é `changed=0`.

```bash
cd ~/vinicius/openran-lab && git fetch origin && git switch feat/stage2-slice-scheduler-poc && git pull
vagrant provision ran 2>&1 | tee /tmp/provision-1.log | tail -4
vagrant provision ran 2>&1 | tee /tmp/provision-2.log | tail -4
vagrant ssh ran
```

**Terminal 1: core.** Confira que os sete NFs montam o arquivo novo e espere o core ficar saudável:

```bash
cd ~/oai-cn5g
docker compose config | grep -c "config.lab.yaml"       # esperado: 7
docker inspect oai-amf --format '{{range .Mounts}}{{.Source}}{{"\n"}}{{end}}' | grep config
docker compose ps --format 'table {{.Name}}\t{{.Status}}'
```

Depois de todos ficarem `healthy`, espere de 30 a 60 segundos (decisão 14).

**Terminal 2: nearRT-RIC.**

```bash
cd ~/openairinterface5g/openair2/E2AP/flexric
./build/examples/ric/nearRT-RIC 2>&1 | tee /tmp/ric.log
```

**Terminal 3: gNB com os dois slices.**

```bash
cd ~/openairinterface5g/cmake_targets/ran_build/build
sudo ./nr-softmodem -O ~/gnb-slices.conf --gNBs.[0].min_rxtxtime 6 --rfsim \
  --e2_agent.near_ric_ip_addr "127.0.0.1" --e2_agent.sm_dir "/usr/local/lib/flexric/" 2>&1 | tee /tmp/gnb.log
```

**Terminal 4: namespaces e UE 1.**

```bash
sudo ~/multi-ue-ns.sh up 2
cd ~/openairinterface5g/cmake_targets/ran_build/build
sudo ip netns exec ue1 ./nr-uesoftmodem -O ~/ue-slice1.conf -r 106 --numerology 1 --band 78 -C 3619200000 --rfsim \
  --rfsimulator.[0].serveraddr 10.201.1.100 2>&1 | tee /tmp/ue1.log
```

**Terminal 5: UE 2**, depois de o UE 1 ter a interface:

```bash
timeout 90 tail -f /tmp/ue1.log | grep -m1 "oaitun_ue1 successfully configured"
cd ~/openairinterface5g/cmake_targets/ran_build/build
sudo ip netns exec ue2 ./nr-uesoftmodem -O ~/ue-slice2.conf -r 106 --numerology 1 --band 78 -C 3619200000 --rfsim \
  --rfsimulator.[0].serveraddr 10.202.1.100 2>&1 | tee /tmp/ue2.log
```

Cada UE cria a própria `oaitun_ue1` dentro do seu namespace, então a linha de espera é a mesma, mas em logs diferentes.

**Terminal 6 (A): slices, ping e tráfego.** Um comando por vez, na ordem.

1. Os dois UEs com interface, endereço e slice. A `oaitun_ue1` de cada UE fica dentro do namespace dele: `ip a` na VM não a mostra.

   ```bash
   timeout 90 tail -f /tmp/ue2.log | grep -m1 "oaitun_ue1 successfully configured"
   for n in 1 2; do sudo ip netns exec ue$n ip -4 -o addr show oaitun_ue1; done
   grep -a -i -E "NGSetup|nssai|sst" /tmp/gnb.log | head -20
   cd ~/oai-cn5g && docker compose logs oai-smf | grep -E "sst, sd" | sort -u
   ```

2. Ping nos dois:

   ```bash
   for n in 1 2; do sudo ip netns exec ue$n ping -c 5 -I oaitun_ue1 192.168.70.135; done
   ```

3. Segundo servidor do iperf3 e IPs dos UEs (as variáveis só valem neste terminal):

   ```bash
   docker exec oai-ext-dn iperf3 -s -p 5202 -D
   UE1_IP=$(sudo ip netns exec ue1 ip -4 -o addr show oaitun_ue1 | awk '{print $4}' | cut -d/ -f1)
   UE2_IP=$(sudo ip netns exec ue2 ip -4 -o addr show oaitun_ue1 | awk '{print $4}' | cut -d/ -f1)
   ```

4. Capacidade de um UE sozinho, em UDP DL (`-R`), acima da capacidade (30 s). Os datagramas têm 1300 bytes (`-l 1300`) porque o MTU da sessão PDU é 1400 (`ue_mtu` do SMF):

   ```bash
   sudo ip netns exec ue1 iperf3 -c 192.168.70.135 -p 5201 -B "$UE1_IP" -u -b 300M -l 1300 -R -t 30 | tail -n 4
   ```

5. Os dois ao mesmo tempo, em segundo plano, por 90 s. Cole as duas linhas juntas e vá direto para o terminal B:

   ```bash
   sudo ip netns exec ue1 iperf3 -c 192.168.70.135 -p 5201 -B "$UE1_IP" -u -b 300M -l 1300 -R -t 90 --logfile /tmp/iperf-ue1.log &
   sudo ip netns exec ue2 iperf3 -c 192.168.70.135 -p 5202 -B "$UE2_IP" -u -b 300M -l 1300 -R -t 90 --logfile /tmp/iperf-ue2.log &
   ```

**Terminal 7 (B): medições, logo depois do passo 5.** Um único comando: espera 10 s para o tráfego estabilizar, mede os recursos da VM durante o tráfego e roda a xApp de KPM por 20 s. Leva cerca de 50 s, dentro dos 90 s do tráfego. Os horários no início e no fim mostram que as medições caíram dentro do teste.

```bash
cd ~/openairinterface5g/openair2/E2AP/flexric && sleep 10 && { \
  echo "inicio $(date +%T)"; free -m; \
  docker stats --no-stream --format 'table {{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}'; \
  mpstat 5 1 | tail -n 1; \
  pidstat -u -C "nr-|nearRT" 5 1 | grep Average; \
  ps -o pid,rss,vsz,pcpu,comm -C nr-softmodem,nr-uesoftmodem,nearRT-RIC; \
  tail -n 20 ~/openairinterface5g/cmake_targets/ran_build/build/nrMAC_stats.log; \
  XAPP_DURATION=20 ./build/examples/xApp/c/monitor/xapp_kpm_moni > /tmp/kpm.log 2>&1; \
  echo "fim $(date +%T)"; } 2>&1 | tee /tmp/medicoes.log
```

O `pidstat -r` não serve para a memória: ele só lista processos com falta de página no intervalo. O RSS vem do `ps`.

**Terminal 6 (A), depois dos 90 s:**

```bash
wait; tail -n 4 /tmp/iperf-ue1.log /tmp/iperf-ue2.log
ls -l --time-style=+%H:%M:%S /tmp/iperf-ue1.log /tmp/iperf-ue2.log
grep -a -E "ran_ue_id|RRU.PrbTotDl|DRB.UEThpDl" /tmp/kpm.log | tail -n 24
```

O horário dos logs do iperf3 marca o fim do tráfego; o início foi 90 s antes. O `-a` do `grep` trata o log da xApp como texto mesmo que ele tenha bytes que o `grep` considere binários.

Ao final, encerre UEs, gNB e RIC com Ctrl+C e remova os namespaces com `sudo ~/multi-ue-ns.sh down 2`.

Se a AMF recusar o NG Setup (`NGSetupFailure` no log do gNB) ou a sessão PDU de um slice (`reject` ou `S-NSSAI` no log da AMF ou do SMF), pare e guarde `docker compose logs oai-amf oai-smf > /tmp/core.log`.

### Resultados

**Rodada 1 (2026-11-23).** OAI upstream `2026.w46`, `~/gnb-slices.conf` sem cotas, rfsim, VM `ran` com 8 vCPU e 16 GB.

| Verificação | Resultado |
| --- | --- |
| Registro e sessão PDU | Os dois UEs registrados na AMF, sem mudar o banco. Sessões no SMF com `1, 000001` (IMSI `...001`, `10.0.0.2`) e `1, 000002` (IMSI `...002`, `10.0.0.3`) |
| `oaitun_ue1` | Criada nos dois namespaces |
| Ping até o ext-dn | 5/5 nos dois; RTT médio de 13,2 ms (UE 1) e 14,9 ms (UE 2) |

| Tráfego UDP DL, `-b 300M -l 1300` | Vazão recebida | Perda |
| --- | --- | --- |
| UE 1 sozinho, 30 s | 95,5 Mbit/s | 66% |
| UE 1, os dois juntos, 90 s | 52,4 Mbit/s (49%) | 81% |
| UE 2, os dois juntos, 90 s | 54,3 Mbit/s (51%) | 80% |
| Soma dos dois juntos | 106,7 Mbit/s | |

O proportional fair dividiu a vazão quase meio a meio (49/51). A soma dos dois (106,7 Mbit/s) passou da vazão de um UE sozinho (95,5 Mbit/s). Uma hipótese é que um UE sozinho seja limitado pela CPU do próprio nrUE no rfsim, e não pelos PRBs; a divisão de PRBs da rodada 2 (KPM e log da MAC) deve esclarecer isso.

| Recursos da VM (rodada 1) | Resultado |
| --- | --- |
| Memória | 5,3 GB usados de 16 GB; 10,3 GB disponíveis; sem swap |
| Contêineres do core | Cerca de 0,67 GB no total (MySQL: 495 MB); CPU baixa (AMF 9%, os outros abaixo de 2%) |
| CPU por processo | gNB 188%, cada nrUE cerca de 113% |
| CPU da VM | 54% ocupada, 46% ociosa |

A CPU por processo foi medida às 18:18:51–56, depois do fim do tráfego (18:18:49); o `mpstat` pegou só os últimos segundos do tráfego. Esses números são do sistema praticamente ocioso: o rfsim mantém gNB e nrUE ocupados mesmo sem tráfego. A rodada 2 repete a medição durante o tráfego, com o comando único do terminal B.

**Rodada 2 (2026-11-24).** Mesmo cenário, com o core reiniciado e as medições num comando único. O tráfego rodou de 13:19:18 a 13:20:48, e as medições de 13:19:29 a 13:20:07, dentro dele.

| Verificação | Resultado |
| --- | --- |
| NG Setup | O gNB anunciou `SST=0x01 SD=0x000001` e `SD=0x000002`; a AMF respondeu com `NGSetupResponse` |
| Sessões | O RRC escolheu o CU-UP por "exact NSSAI match" `1:0x000001` (UE 1) e `1:0x000002` (UE 2) |

| Tráfego UDP DL, os dois juntos, 90 s | Vazão recebida (iperf3) | Perda | Goodput DL na MAC (instantâneo) |
| --- | --- | --- | --- |
| UE 1 (slice 1) | 54,4 Mbit/s (49,4%) | 80% | 45,8 Mbit/s |
| UE 2 (slice 2) | 55,7 Mbit/s (50,6%) | 79% | 45,8 Mbit/s |
| Soma | 110,1 Mbit/s | | |

Na MAC, os dois UEs ficaram com MCS 28 (64QAM), CQI 15, BLER 0 e cerca de 71 mil transmissões DL cada. Houve cerca de 1.400 falhas de alocação de CCE por UE, o que mostra disputa pelo PDCCH com dois UEs; por isso o escalonador por slice alterna a ordem dos slices a cada slot. A divisão sem cotas (proportional fair puro) é **49/51**, nas duas rodadas.

| Recursos da VM com os dois UEs saturados | Resultado |
| --- | --- |
| CPU da VM (`mpstat`) | **93% ocupada** (usuário 62%, sistema 26%, softirq 5%), 7% ociosa |
| CPU por processo (`pidstat`) | gNB 220%, cada nrUE cerca de 213%: cerca de 6,5 núcleos |
| CPU no core | UPF 31%, `oai-ext-dn` (iperf3) 26%, MySQL 9%, os outros perto de zero |
| Memória (RSS) | gNB 3,4 GB, cada nrUE 456 MB, nearRT-RIC 5 MB |
| Memória da VM | 5,3 GB usados de 16 GB; 10,3 GB disponíveis; sem swap |

Com os dois UEs saturados, os 8 vCPU ficam no limite, e a vazão provavelmente é limitada pela CPU, não pelos PRBs. Por isso a divisão de PRBs nos testes T1 a T4 deve ser medida pelo log do escalonador, e não só pela vazão. Mandar 300 Mbit/s por UE, cerca de 5 vezes a fatia de cada um, gasta CPU à toa: os pacotes passam pelo UPF e pelo gNB e são descartados no RLC.

A memória não é problema: sobram cerca de 10 GB.

**KPM por UE: a `xapp_kpm_moni` não vê UEs fora de `1.ffffff`.** A xApp assina o relatório por UE (REPORT estilo 4) com a condição S-NSSAI contendo só o SST (`xapp_kpm_moni.c:279`). O agente E2 do OAI trata uma S-NSSAI sem SD como `1.ffffff` (`nssai_matches()`, `openair2/E2AP/RAN_FUNCTION/O-RAN/ran_func_kpm.c:186`). Como os UEs estão em `1.000001` e `1.000002`, nenhum entra no relatório; o KPM de célula (estilo 1, por exemplo `CARR.PDSCHMCSDist`) chega normalmente. O agente já aceita uma S-NSSAI com SD, de 4 octetos (`capture_sst_sd()`, linha 171). Para ter KPM por UE, a xApp precisa assinar uma vez por slice, com o SD.

## Testes da PoC

Os testes usam o gNB compilado do fork, no commit fixado em `oai_fork_commit`.

### O que o provisionamento prepara

Com `oai_slice_scheduler_enabled: true` em `ansible/group_vars/all.yml`, a role `oai_ran`:

- põe o clone do OAI no commit do fork e recompila. Só faz isso com o clone sem mudanças locais; com a chave em `false`, volta ao commit da tag;
- gera um `.conf` do gNB por perfil de cotas, a partir do `~/gnb-slices.conf`, com a seção `slice_policies` dentro de `MACRLCs`;
- copia `scripts/slice-prb-share.sh` para a home, que resume as linhas `[SLICE]` de um trecho do log do gNB.

| Arquivo | Cotas (slice 1 / slice 2) | Testes |
| --- | --- | --- |
| `~/gnb-slices.conf` | sem `slice_policies` | T0 |
| `~/gnb-slices-30-70.conf` | `max_ratio` 30 / 70 | T1, T2, T5, T6 |
| `~/gnb-slices-min60.conf` | `min_ratio` 60 / 0, `max_ratio` 100 / 100 | T3 |
| `~/gnb-slices-ded40.conf` | `dedicated_ratio` e `min_ratio` 40 / 0, `max_ratio` 100 / 100 | T4 |
| `~/gnb-slices-100-100.conf` | `max_ratio` 100 / 100 | controle: escalonador ligado, sem limite |

Os perfis ficam em `oai_ran_gnb_quota_profiles`, nos defaults da role. Durante a PoC, a chave era ligada só na cópia local do servidor; desde a [decisão 23](../decisions.md#23-fork-do-oai-como-padrão), ela fica ligada por padrão.

### Roteiro

No servidor, provisione duas vezes. A primeira recompila; na segunda, o esperado é `changed=0`.

```bash
cd ~/vinicius/openran-lab && git pull
vagrant provision ran 2>&1 | tee /tmp/provision-fork-1.log | tail -4
vagrant provision ran 2>&1 | tee /tmp/provision-fork-2.log | tail -4
```

Core, RIC, namespaces e UEs sobem como na [linha de base](#roteiro). Muda só o gNB, que lê o perfil da variável `CONF`. A variável vale só no terminal onde foi definida, então defina-a **no terminal do gNB**:

```bash
CONF=~/gnb-slices-30-70.conf
cd ~/openairinterface5g/cmake_targets/ran_build/build && sudo ./nr-softmodem -O "$CONF" --gNBs.[0].min_rxtxtime 6 --rfsim \
  --e2_agent.near_ric_ip_addr "127.0.0.1" --e2_agent.sm_dir "/usr/local/lib/flexric/" 2>&1 | tee /tmp/gnb.log
```

As cotas só são lidas na subida. A cada troca de perfil, encerre UEs e gNB e suba os três de novo.

No terminal de testes, suba o segundo servidor do iperf3 e defina as duas funções. Elas valem enquanto o terminal estiver aberto; depois de reiniciar a VM, defina de novo, junto com os namespaces (`sudo ~/multi-ue-ns.sh up 2`).

```bash
docker exec oai-ext-dn iperf3 -s -p 5202 -D
ue_ip() { sudo ip netns exec "ue$1" ip -4 -o addr show oaitun_ue1 | awk '{print $4}' | cut -d/ -f1; }
janela() { local nome=$1 dur=$2 rate=$3 L n pids=(); shift 3; L=$(wc -l < /tmp/gnb.log); for n in "$@"; do sudo ip netns exec "ue$n" iperf3 -c 192.168.70.135 -p "520$n" -B "$(ue_ip "$n")" -u -b "$rate" -l 1300 -R -t "$dur" --logfile "/tmp/iperf-$nome-ue$n.log" & pids+=($!); done; wait "${pids[@]}"; for n in "$@"; do echo "== UE$n"; tail -n 4 "/tmp/iperf-$nome-ue$n.log"; done; tail -n +"$L" /tmp/gnb.log | ~/slice-prb-share.sh -s 3 | tee "/tmp/$nome.txt"; }
```

`janela NOME SEGUNDOS TAXA UE...` marca a linha atual do log do gNB, roda o iperf3 nos UEs pedidos ao mesmo tempo e, no fim, resume só o trecho do teste. O `-s 3` descarta os três primeiros períodos, enquanto o tráfego sobe. Antes de cada teste, confira o perfil e os IPs:

```bash
grep -a "cota DL" /tmp/gnb.log; ue_ip 1; ue_ip 2
```

| Teste | Perfil | Comando |
| --- | --- | --- |
| T0 | `gnb-slices.conf`, só o UE 1 | `grep -a -c "\[SLICE\]" /tmp/gnb.log` (esperado: 0), ping e iperf3 como na linha de base, `xapp_kpm_moni` |
| T1 | `30-70` | `janela t1 60 200M 1 2` |
| T2 | `30-70` | `janela t2 60 200M 1` |
| T3 | `min60` | `janela t3 60 200M 1 2` |
| T4 | `ded40` | `janela t4 60 200M 2` |
| T5 e T6 | `30-70` | `janela t5 600 200M 1 2` e, cerca de 1 min depois, num segundo terminal, o comando de medição da linha de base com a `xapp_kpm_moni`. No fim: `grep -a -c -E "Assert\|Segmentation" /tmp/gnb.log; pgrep -a nr-softmodem` |
| Controle c1 | `100-100` | `janela c1 60 200M 1` |
| Controle c2 | `100-100` | `janela c2 60 200M 1 2` |

O resumo do `slice-prb-share.sh` traz duas porcentagens por slice. **`%disp`** é a fatia dos PRBs livres para PDSCH, somados slot a slot; compare-a com as cotas em relação ao teto prático de um UE (veja abaixo). **`%usados`** é a fatia dos PRBs efetivamente alocados; serve para a divisão entre slices que disputam.

### Resultados da PoC

**Rodadas de 2026-11-24 e 2026-11-25.** Fork no commit `125c80f`, mesma VM e mesmo cenário da linha de base. Os testes T2, T3, T5 e c2 rodaram com `-b 100M`; neles, os UEs que precisavam saturar saturaram, com perda de 35% a 63%. T1, T4 e c1 rodaram com `-b 200M`.

| Teste | Resultado | Critério |
| --- | --- | --- |
| T0 Regressão | Nenhuma linha `[SLICE]`; ping 5/5; KPM de célula recebido. Com os dois UEs e sem cotas, 54,9 / 54,5 Mbit/s, contra 54,4 / 55,7 na linha de base | Passou |
| T1 Disputa | Os dois saturados (perda de 78% e 61%): **31,6 / 68,4** dos PRBs usados; 39,1 / 73,4 Mbit/s | Passou (30/70 ± 5 p.p.) |
| T2 Conservação | Só o UE 1, saturado (perda de 52%): slice 1 em 22,3% dos disponíveis, que são 30% do teto de um UE; 42,7 Mbit/s, 31% dos 137 Mbit/s de um UE sem cotas | Passou |
| T3 Mínimo | Com o mínimo de 60%, o UE 1 recebeu tudo o que pediu (100 Mbit/s, perda de 0,3%), e o UE 2 ficou com 45 Mbit/s. No controle c2, com a mesma taxa pedida, o UE 1 recebia 60 Mbit/s | Passou |
| T4 Dedicado | Só o UE 2, saturado (perda de 61%), com 40% dedicados ao slice 1 ocioso: slice 2 em 45,6% dos disponíveis, **61%** do teto de um UE | Passou (até 60% do teto) |
| T5 Estabilidade | 10 min com os dois UEs e o perfil `30-70`: nenhum `Assert`, gNB no ar | Passou |
| T6 E2 | `xapp_kpm_moni` recebeu o KPM de célula durante o T5 (`Test xApp run SUCCESSFULLY`) | Passou |

| Controle | Resultado |
| --- | --- |
| c1: um UE sozinho, sem limite | 108 Mbit/s (perda de 44%), **75,0%** dos PRBs disponíveis |
| c2: os dois, sem limite | **49,8 / 50,2** dos PRBs usados; cerca de 60 Mbit/s cada |
| Mínimo e dedicado sob disputa (perfil `ded40`, os dois saturados) | **68 / 32** dos usados. Previsto: 70/30 (42 PRBs dedicados ao slice 1, os 64 restantes em partes iguais) |

Recursos durante o T5, com cotas, comparados com o fork sem cotas: CPU da VM de 61 / 25 / 3,7% (usuário, sistema, softirq) contra 62 / 24 / 3,8%; gNB com 218% nos dois casos; RSS do gNB de 3,52 GB contra 3,57 GB. O custo do escalonador não aparece na medição.

**O teto prático de um UE é de cerca de 75% dos PRBs disponíveis.** Mesmo sem limite (c1), um UE saturado usa 75% dos PRBs livres para PDSCH contados pelo escalonador. Por isso, com `max_ratio` 30, o slice 1 fica sempre em 22,3% dos disponíveis: 30% de 75%. O teto é aplicado em cada slot; os cerca de 25% restantes não são usados por ninguém, com ou sem cotas. O `nrMAC_stats.log` mostra só 2 falhas de CCE por UE, então o PDCCH não explica a diferença. A causa fica em aberto (veja abaixo).

**No rfsim, a simulação roda cerca de 2,4 vezes mais rápido que o relógio.** O gNB escreve uma linha de período a cada 100 frames (1 s de tempo simulado), e saíram 70 a 71 linhas em 30 s de relógio. Os Mbit/s do iperf3 são de relógio de parede e não se comparam com a capacidade teórica da célula. As porcentagens de PRBs não são afetadas.

**A vazão de um UE sozinho depende de o outro nrUE estar ligado.** Com só o UE 1 ligado (T0), ele fez 137 Mbit/s; na linha de base, com o UE 2 ligado e ocioso, 95,5 Mbit/s. O segundo `nr-uesoftmodem` disputa a CPU da VM. Compare vazões só entre cenários com os mesmos processos no ar.

### Pontos em aberto

- **Os cerca de 25% de PRBs que nenhum UE usa.** Candidatos: slots em que o UE não pode ser escalonado (processos HARQ esperando confirmação, restrições de `k1`), o slot misto do TDD contado como cheio, ou PRBs livres no `vrb_map` que o proportional fair não aproveita. Investigar com o `nrMAC_stats.log` lido durante o tráfego e, se preciso, um contador por slot no log do escalonador.
- **Fork como padrão.** Ligado em 2026-11-25, depois da PoC e da [xApp de monitoração KPM por slice](#xapp-de-monitoração-kpm-por-slice) ([decisão 23](../decisions.md#23-fork-do-oai-como-padrão)).
- **Controle pela E2.** Pronto: o [controle de cotas pelo E2SM-RC](#controle-de-cotas-pelo-e2sm-rc) passou nos critérios C1 a C5.

## xApp de monitoração KPM por slice

Primeira peça da xApp de slicing ([decisão 21](../decisions.md#21-xapp-de-monitoração-kpm-por-slice)). A `slice_kpm_monitor` assina, para cada slice, o REPORT estilo 4 do E2SM-KPM com a S-NSSAI de 4 octetos (SST + SD). A cada segundo, soma por slice as medidas por UE `RRU.PrbTotDl`, `RRU.PrbTotUl`, `DRB.UEThpDl` e `DRB.UEThpUl`. O código fica em [`xapps/slice_kpm_monitor/`](../../xapps/slice_kpm_monitor/), sob a Apache-2.0:

| Parte | Arquivo | Papel |
| --- | --- | --- |
| Núcleo | `src/skm_core.{h,c}` | S-NSSAI, soma por slice, descarte do primeiro relatório de cada UE, saída em texto e JSON Lines. Não depende de RIC; tem testes em `tests/` |
| Adaptador FlexRIC | `src/skm_flexric.c` | Conecta pela E42, monta as assinaturas e converte as indicações para o núcleo. Portar para outro Near-RT RIC é trocar só este arquivo |

### Correção no agente E2 do fork

Com uma assinatura por slice, o agente E2 do OAI `2026.w46` calculava errado as medidas por UE:

- os contadores do relatório anterior ficavam em vetores globais, indexados pela posição do UE na lista de cada relatório;
- a referência de PRBs da célula era uma só para todas as assinaturas.

O UE do slice 1 e o do slice 2, ambos na posição 0 da sua assinatura, usavam o mesmo contador, e cada relatório reiniciava a referência das outras assinaturas.

A branch `kpm-per-slice` do fork (commit `12537539`, a partir de `slice-scheduler`) guarda esse estado por assinatura e por UE, em `ran_func_kpm_state.{h,c}`. O primeiro relatório de cada UE ainda parte de zero, como no upstream; a xApp o descarta. O commit seguinte da branch, `95a3e328`, atualiza o submódulo do FlexRIC (seção abaixo) e é o fixado em `oai_fork_commit`.

### Correção no RIC do FlexRIC

No K4, com a `slice_kpm_monitor` e a `xapp_kpm_moni` rodando juntas, o cancelamento das assinaturas de uma xApp apagava as da outra. O RIC traduz o `RIC_REQ_ID` de cada xApp (as duas numeram a partir de 1) para um ID próprio com o gNB, e a função que compara os pedidos, `eq_xapp_ric_gen_id` em `src/ric/iApp/xapp_ric_id.c`, compara a xApp com ela mesma:

```c
if(eq_xapp_id(m0->xapp_id, m0->xapp_id) == false)   // devia ser m1->xapp_id
```

Com isso, o RIC ignora de qual xApp vem o pedido. O cancelamento da assinatura 1 da `xapp_kpm_moni` caía na primeira assinatura 1 do mapa, a da `slice_kpm_monitor`. A xApp que perdeu as assinaturas parava de receber indicações e abortava com `assoc_rb_tree_extract ... key not found`; a outra abortava por timeout. O agente E2 do gNB funcionava normalmente.

O upstream do FlexRIC corrigiu esse erro no commit `ea6d9b4` (2026-11-13), 19 commits depois do `18c3325` que a tag `2026.w46` do OAI fixa. O commit `95a3e328` da branch `kpm-per-slice` do fork aponta o submódulo para o `ea6d9b4`, e o provisionamento recompila o RIC e o gNB ([decisão 22](../decisions.md#22-correção-do-ric-do-flexric)). As outras mudanças entre os dois commits são documentação, arquivos de CI, a `xapp_kpm_moni` (que passa a pular os estilos de REPORT sem suporte) e uma linha do TC SM.

Se você aplicou a correção à mão na VM (o `sed` no `xapp_ric_id.c`), desfaça antes do `vagrant provision`, porque a role `oai_ran` exige o clone limpo: `git -C ~/openairinterface5g/openair2/E2AP/flexric checkout -- src/ric/iApp/xapp_ric_id.c`.

### Compilação na VM

A role `xapps` copia cada pasta de `xapps/` para `~/openran-lab-xapps/` na VM, compila contra o FlexRIC do submódulo do OAI (com o mesmo gcc-12), roda os testes (`ctest`) e põe o executável no `PATH` (`/usr/local/bin`). O build fica em `~/xapps-build/<xApp>/`. A role recompila quando o código da xApp ou o commit do FlexRIC mudam.

A `libe42_xapp_shared.so` do FlexRIC não inclui o runtime ASN.1 nem as mensagens RRC, que ficam na biblioteca estática `libasn1_nr_rrc.a`, compilada com os exemplos de xApp. O `CMakeLists.txt` da xApp liga as duas.

Para compilar à mão, por exemplo ao mexer no código direto na VM:

```bash
FR=~/openairinterface5g/openair2/E2AP/flexric
CC=gcc-12 cmake -S ~/openran-lab-xapps/slice_kpm_monitor -B ~/xapps-build/slice_kpm_monitor -DFLEXRIC_SRC_DIR=$FR -DFLEXRIC_BUILD_DIR=$FR/build
cmake --build ~/xapps-build/slice_kpm_monitor -j4 && ctest --test-dir ~/xapps-build/slice_kpm_monitor --output-on-failure
```

Uma xApp nova entra em `xapps_list`, nos defaults da role, e segue a mesma convenção: um `CMakeLists.txt` que aceita `FLEXRIC_SRC_DIR` e `FLEXRIC_BUILD_DIR` e gera um executável com o nome da pasta.

### Uso

```bash
XAPP_DURATION=60 slice_kpm_monitor --slice 1.000001 --slice 1.000002 --json /tmp/skm.jsonl --per-ue
```

A saída tem uma linha por slice por relatório, por exemplo:

```
slice 1.000001  UEs 1 (novos 0)  prb_dl_pct 22.0  prb_ul_pct 1.0  thp_dl_kbps 39100.0  thp_ul_kbps 120.0
```

O JSON Lines (`--json`) tem os mesmos campos, mais o início da coleta em `ts_us`. É o formato que o produtor Y1 da Etapa 4 vai ler. A xApp acrescenta ao fim do arquivo: apague-o antes de cada teste (`rm -f /tmp/skm.jsonl`), ou as médias misturam rodadas. Um slice sem UE não gera linha, porque o agente E2 não envia indicação vazia.

### Critérios de aceite

| Teste | Cenário | Critério |
| --- | --- | --- |
| K1 Separação | Perfil `30-70`, os dois UEs conectados | Uma linha por slice por segundo, cada uma com o UE dele (`--per-ue`) |
| K2 PRBs | Perfil `30-70`, os dois saturados (`janela k2 60 200M 1 2`) | `prb_dl_pct` de cada slice, na média, a até 3 p.p. do `%disp` do `slice-prb-share.sh`, com as duas medições no mesmo intervalo |
| K3 Vazão | Perfil `30-70`, os dois sem saturação (`janela k3b 60 20M 1 2`) | `thp_dl_kbps` de cada slice a até 5% da vazão do iperf3 do UE. Com saturação, o KPM mostra o que o RLC do gNB transmite e fica acima do iperf3 |
| K4 Convivência | K2 com a `xapp_kpm_moni` rodando junto | Os números da `slice_kpm_monitor` não mudam; as duas xApps terminam sem erro; nenhum assert no gNB. Exige a [correção do RIC](#correção-no-ric-do-flexric) |

### Resultados (2026-11-25)

Fork `12537539` (`kpm-per-slice`), perfil `30-70`, dois UEs. No K1 a K4, UDP de 200 Mbit/s por UE (saturados), e a xApp mede 40 s no meio dos 60 s da `janela`. No K2b e no K3b, a xApp começa antes do tráfego e termina depois dele, e as médias usam só os relatórios com tráfego (`thp_dl_kbps` acima de 5000), o mesmo intervalo da `janela`.

| Teste | Resultado | Situação |
| --- | --- | --- |
| K1 | Uma linha por slice por segundo; o slice `1.000001` mostra só o UE 1 e o `1.000002` só o UE 2. Sem tráfego, `prb_dl_pct` 0 e `prb_ul_pct` de 3% a 4% | Passou |
| K2 | Slice 1: 22,0% contra `%disp` de 22,3% a 22,4% nas quatro rodadas. Slice 2: de 51,0% a 52,4% contra 48,3% a 49,5% (+2,6 a +3,2 p.p.), com intervalos diferentes | Ver K2b |
| K2b | Mesmo intervalo: slice 1 com 21,8% contra 22,3% (−0,5 p.p.); slice 2 com 50,8% contra 48,2% (+2,6 p.p.) | Passou |
| K3 | Saturado, o KPM fica acima do iperf3: 48,4 contra 36,5 Mbit/s no slice 1 e 117,5 contra 75,1 Mbit/s no slice 2 | Esperado (ver abaixo) |
| K3b | 20 Mbit/s por UE, sem perda no iperf3: KPM com 20,5 contra 20,1 Mbit/s (+2,0%) no slice 1 e 20,2 contra 20,0 Mbit/s (+1,0%) no slice 2 | Passou |
| K4 | Com a correção do RIC: médias de 22,0% e 52,3% (iguais às do K2), 39 relatórios por slice, `xapp_kpm_moni` com `SUCCESSFULLY` e nenhum assert no gNB. Sem a correção, as duas xApps abortavam no cancelamento | Passou |
| K4 provisionado | Fork `95a3e328` e FlexRIC `ea6d9b4`, compilados pelo provisionamento (role `xapps` para a xApp): médias de 22,0% e 51,0%, 39 relatórios por slice, `xapp_kpm_moni` com `SUCCESSFULLY` e nenhum assert no gNB | Passou |

- **K2:** o `slice-prb-share.sh` cobre os 60 s inteiros, incluindo o começo e o fim do tráfego; a xApp, só 40 s com o tráfego estável. O slice 1 fica no teto de 30% e não muda; o slice 2, que ocupa o que sobra, sente a diferença. O critério passa a comparar as duas medições no mesmo intervalo (K2b). Mesmo assim, o slice 2 fica cerca de 2,5 p.p. acima no KPM, dentro da tolerância; a causa não foi investigada.
- **K3:** no agente E2, o `DRB.UEThpDl` é calculado com os bytes de PDU que o RLC do gNB transmitiu (`rlc.txpdu_bytes`). Com saturação, isso inclui retransmissões, cabeçalhos e pacotes que o nrUE perde antes de chegar ao iperf3 (perda de 60% a 79%). A razão entre os slices pelo KPM (2,43) acompanha a dos PRBs (2,38), não a do iperf3 (2,06): o KPM mostra o que o gNB transmite. O critério passa a valer sem saturação (K3b), em que a diferença de 1% a 2% corresponde aos cabeçalhos IP e UDP que o iperf3 não conta.
- **PRBs sem saturação:** no K3b, o KPM mostra 7,3% e 8,0% dos PRBs, e o `slice-prb-share.sh`, 14,9% e 15,5%. As bases são diferentes: o KPM divide pelos PRBs de todos os slots DL (106 por slot), e o `%disp` do escalonador, pelos PRBs livres só nos slots em que ele distribuiu PRBs entre os slices. Com saturação, as duas bases coincidem; sem saturação, a do escalonador fica menor (88 mil contra 168 mil PRBs por período). Por isso o K2 só vale com os slices saturados.

## Controle de cotas pelo E2SM-RC

Última peça da Etapa 2 ([decisão 25](../decisions.md#25-controle-de-cotas-pelo-e2sm-rc-estilo-2)): uma xApp muda as cotas de PRB por slice pela E2, sem reiniciar o gNB. O gNB anuncia o CONTROL estilo 2 do E2SM-RC, "Radio Resource Allocation Control", com a ação 6, "Slice-level PRB quota" (E2SM-RC v1.03, seções 7.6.3 e 8.4.3.6), e aplica as cotas com a `nr_slice_policy_set()` do escalonador por slice.

### Handler no fork

A branch `rc-slice-ctrl` do fork (commit `000b2090`, a partir de `kpm-per-slice`) traz:

- **Anúncio no E2 Setup:** o estilo 2, só com a ação 6, e a árvore de parâmetros abaixo. Só nós com MAC (gNB monolítico ou DU) anunciam.
- **Conversão da mensagem** em `ran_func_rc_slice.{h,c}`, sem depender do MAC, com testes GTest em `O-RAN/tests/` (`-DENABLE_TESTS=ON`).
- **Handler** em `ran_func_rc.c`. Ele atualiza só os slices da mensagem e mantém as cotas dos outros. A cota neutra (`min 0, max 100, ded 0`) remove a cota do slice; sem nenhum slice com cota, o DL volta ao proportional fair. Não há mensagem para remover todas de uma vez, porque um parâmetro LIST do E2SM-RC precisa de ao menos um item. Ele exige o PLMN de uma célula do gNB e valida o resultado com as regras do escalonador. Uma mensagem inválida não muda nada.

| ID | Parâmetro | Tipo | Valor |
| --- | --- | --- | --- |
| 1 | RRM Policy Ratio List | LIST | Um item por grupo de cotas |
| 2 | RRM Policy Ratio Group | STRUCTURE (item) | 3, 10, 11 e 12 |
| 3 | RRM Policy | STRUCTURE | 4 |
| 4 | RRM Policy Member List | LIST | Um item por slice |
| 5 | RRM Policy Member | STRUCTURE (item) | 6 e 7 |
| 6 | PLMN Identity | ELEMENT | 3 octetos BCD (TS 38.413) |
| 7 | S-NSSAI | STRUCTURE | 8 e, se houver, 9 |
| 8 | SST | ELEMENT | 1 octeto (ou INTEGER) |
| 9 | SD | ELEMENT, opcional | 3 octetos (ou INTEGER) |
| 10 | Min PRB Policy Ratio | ELEMENT, opcional | 0 a 100, padrão 0 |
| 11 | Max PRB Policy Ratio | ELEMENT, opcional | 0 a 100, padrão 100 |
| 12 | Dedicated PRB Policy Ratio | ELEMENT, opcional | 0 a 100, padrão 0 |

Nos valores, os itens de uma LIST (2 e 5) não levam ID: cada item já é a STRUCTURE com os membros dele. Os IDs 2 e 5 só aparecem na definição anunciada.

**Resposta ao CONTROL:** o agente E2 do FlexRIC responde `CONTROL ACKNOWLEDGE` mesmo quando o gNB recusa as cotas, porque não tem caminho para `CONTROL FAILURE`. O resultado fica no log do gNB:

- cotas aplicadas: linhas `[SLICE] cota DL do slice ...` seguidas de `[SLICE] E2SM-RC: Slice-level PRB quota aplicada`;
- recusa: `[SLICE] E2SM-RC: Slice-level PRB quota recusada: <motivo>`.

### xApp `slice_quota_ctrl`

Fica em [`xapps/slice_quota_ctrl/`](../../xapps/slice_quota_ctrl/), sob a Apache-2.0, com o mesmo desenho da `slice_kpm_monitor`:

| Parte | Arquivo | Papel |
| --- | --- | --- |
| Núcleo | `src/sqc_core.{h,c}` | Lê as cotas (`SST.SD:min:max:ded`) e o PLMN, valida com as regras do gNB e codifica o PLMN e o SD. Não depende de RIC; tem testes em `tests/` |
| Adaptador FlexRIC | `src/sqc_flexric.c` | Confere se o E2 node anuncia a ação 6, monta o CONTROL e o envia |

É uma ferramenta de linha de comando: manda um CONTROL e termina.

```bash
slice_quota_ctrl --check                                              # mostra o anúncio do estilo 2
slice_quota_ctrl --quota 1.000001:0:70:0 --quota 1.000002:0:30:0      # muda as cotas dos dois slices
slice_quota_ctrl --remove 1.000001 --remove 1.000002                  # remove as cotas dos dois slices
```

O PLMN padrão é `001.01`; outro vai em `--plmn MCC.MNC`. A xApp recusa, antes de enviar, cotas fora de `ded <= min <= max <= 100`, S-NSSAI repetida ou soma dos `min` acima de 100. O gNB confere de novo, já com as cotas dos outros slices.

### Compilação na VM

Desde a validação (C1 a C5), o provisionamento faz tudo: `oai_fork_commit` aponta para o commit `000b2090` da branch `rc-slice-ctrl`, e a role `xapps` compila a `slice_quota_ctrl` junto com a `slice_kpm_monitor` e põe as duas no `PATH`. Para compilar à mão, por exemplo ao mexer no código direto na VM, os comandos são os mesmos da [`slice_kpm_monitor`](#compilação-na-vm), trocando o nome da pasta.

### Critérios de aceite

| Teste | Cenário | Critério |
| --- | --- | --- |
| C1 Anúncio | gNB da branch `rc-slice-ctrl`, `slice_quota_ctrl --check` | A xApp mostra o estilo 2, a ação 6 e a árvore de parâmetros 1 a 12 |
| C2 Troca com tráfego | Perfil `30-70`, os dois UEs saturados; no meio do tráfego, CONTROL para 70 / 30 | Linhas `cota DL` novas e `aplicada` no log; `prb_dl_pct` da `slice_kpm_monitor` troca de lado (cerca de 22% e 52% para 52% e 22%) em até 2 relatórios |
| C3 Recusa | CONTROL com `min` 60 nos dois slices, montado sem a validação da xApp | O log mostra `recusada` com o motivo; as cotas não mudam; o gNB continua no ar |
| C4 Remoção | `slice_quota_ctrl --remove 1.000001 --remove 1.000002` com tráfego | Linha `cotas por slice desligadas` no log; a divisão de PRBs passa à de sem cotas |
| C5 Convivência | C2 a C4 com a `slice_kpm_monitor` rodando | Nenhum erro nas xApps; nenhum assert no gNB |

No C3, a xApp recusa sozinha a soma de mínimos acima de 100. Para o gNB receber a mensagem, o teste usa dois CONTROLs válidos em sequência, que juntos passam do limite: primeiro `--quota 1.000001:60:100:0`, depois `--quota 1.000002:60:100:0`. O segundo só é inválido somado às cotas em vigor, e é o gNB que o recusa.

### Resultados (2026-11-27)

Fork no commit `c6e76b9f` para C1 a C3 e a primeira tentativa do C4; o C4 e o C5 passaram com a correção da remoção (abaixo), no `000b2090`. Perfil `30-70` na subida, os dois UEs com UDP de 200 Mbit/s cada.

| Teste | Resultado | Situação |
| --- | --- | --- |
| C1 | A `slice_quota_ctrl --check` mostrou o estilo 2, a ação 6 e a árvore de parâmetros 1 a 12 na hierarquia certa; o E2 Setup com o anúncio novo não gerou nenhum assert | Passou |
| C2 | Com 30 / 70: KPM 22% / 51%, `%disp` 22,3% / 48,6%, iperf3 38,4 / 74,2 Mbit/s. Depois do CONTROL para 70 / 30: KPM 51% / 22%, `%disp` 48,7% / 22,3%, iperf3 74,8 / 37,5 Mbit/s. Numa rodada com a troca no meio, os dois UEs ficaram com a média (56,2 / 58,6 Mbit/s). O slice que perdeu cota foi de 52% para 22% com um único relatório de transição (42%) | Passou |
| C3 | O primeiro CONTROL (`1.000001` com `min` 60) foi aplicado e manteve a cota do slice 2 (`1 slice(s) na mensagem, 2 com cota`); o segundo foi recusado pelo gNB com `soma dos min_ratio (120) maior que 100`, sem mudar as cotas; gNB no ar, sem asserts | Passou |
| C4 | Primeira versão: o `--clear` abortou na xApp, no codificador ASN.1 do FlexRIC (`rc_enc_ctrl_msg_asn ... Failed to encode`), sem chegar ao gNB; até ali, com `min` 60 no slice 1 e `max` 30 no slice 2, KPM 59% / 15% (129 / 33 Mbit/s). Com o `--remove` (fork `000b2090`): de 30 / 70, KPM 22% / 52% e 49 / 120 Mbit/s; depois de remover as duas cotas, **38% / 38%** e 85 / 85 Mbit/s, com um relatório de transição; log com `cotas por slice desligadas` e `aplicada (2 slice(s) na mensagem, 0 com cota)` | Passou |
| C5 | Do C2 ao C4, nenhum assert no gNB e nenhum erro nos logs da `slice_kpm_monitor` | Passou |

**Remoção das cotas.** No E2SM-RC, um parâmetro LIST precisa ter ao menos um item, então a lista vazia que a primeira versão usava para "remover todas as cotas" não é uma mensagem válida. Os testes do fork não pegaram o problema porque montam a mensagem direto na estrutura interna, sem o codificador. A correção (fork `000b2090`) usa a cota neutra, `min 0, max 100, ded 0`, que são os valores padrão do RRMPolicyRatio e não limitam nada: o gNB tira da lista o slice que a recebe e, sem nenhum slice com cota, volta à alocação padrão. Na xApp, o `--clear` virou `--remove SST.SD`, que pode se repetir.

**UE que cai por inatividade.** Numa rodada, os UEs ficaram minutos sem tráfego depois da subida; a rede liberou a conexão do UE 1, e, quando o iperf3 começou, o core recusou o pedido de volta do UE (`Service Reject ... UE_identity_cannot_be_derived_by_the_network`) e o nrUE encerrou. Para os testes, um ping de fundo em cada UE mantém a conexão ([troubleshooting](../troubleshooting.md)):

```bash
for n in 1 2; do sudo ip netns exec ue$n ping -I oaitun_ue1 -i 1 192.168.70.135 > /dev/null 2>&1 & done
```

A `janela` espera só os próprios iperf3 (`wait` com os PIDs), para funcionar com esses pings no mesmo terminal.
