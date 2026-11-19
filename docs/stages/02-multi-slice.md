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

**Terminal 6: verificações e tráfego.**

1. Os dois UEs com interface, endereço e slice:

   ```bash
   timeout 90 tail -f /tmp/ue2.log | grep -m1 "oaitun_ue1 successfully configured"
   for n in 1 2; do sudo ip netns exec ue$n ip -4 -o addr show oaitun_ue1; done
   grep -i -E "NGSetup|nssai|sst" /tmp/gnb.log | head -20
   cd ~/oai-cn5g && docker compose logs oai-amf | grep -i -E "nssai|slice|reject|failure" | tail -20
   docker compose logs oai-smf | grep -i -E "snssai|sst" | tail -20
   ```

2. Ping nos dois:

   ```bash
   for n in 1 2; do sudo ip netns exec ue$n ping -c 5 -I oaitun_ue1 192.168.70.135; done
   ```

3. Capacidade de um UE sozinho e depois os dois ao mesmo tempo, em UDP DL (`-R`), acima da capacidade. O `oai-ext-dn` já roda um servidor na porta 5201; o segundo fica na 5202. Os datagramas têm 1300 bytes (`-l 1300`) porque o MTU da sessão PDU é 1400 (`ue_mtu` do SMF):

   ```bash
   docker exec oai-ext-dn iperf3 -s -p 5202 -D
   UE1_IP=$(sudo ip netns exec ue1 ip -4 -o addr show oaitun_ue1 | awk '{print $4}' | cut -d/ -f1)
   UE2_IP=$(sudo ip netns exec ue2 ip -4 -o addr show oaitun_ue1 | awk '{print $4}' | cut -d/ -f1)
   sudo ip netns exec ue1 iperf3 -c 192.168.70.135 -p 5201 -B "$UE1_IP" -u -b 300M -l 1300 -R -t 30 | tail -4
   sudo ip netns exec ue1 iperf3 -c 192.168.70.135 -p 5201 -B "$UE1_IP" -u -b 300M -l 1300 -R -t 60 --logfile /tmp/iperf-ue1.log &
   sudo ip netns exec ue2 iperf3 -c 192.168.70.135 -p 5202 -B "$UE2_IP" -u -b 300M -l 1300 -R -t 60 --logfile /tmp/iperf-ue2.log &
   ```

4. Com o tráfego dos dois rodando, no terminal 7, KPM por UE:

   ```bash
   cd ~/openairinterface5g/openair2/E2AP/flexric
   XAPP_DURATION=20 ./build/examples/xApp/c/monitor/xapp_kpm_moni 2>&1 | tee /tmp/kpm.log | tail -5
   grep -E "ran_ue_id|RRU.PrbTotDl|DRB.UEThpDl" /tmp/kpm.log | tail -24
   ```

5. Ainda com o tráfego rodando, recursos da VM, no terminal 7:

   ```bash
   free -m
   docker stats --no-stream --format 'table {{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}'
   mpstat 5 1 | tail -1
   pidstat -u -r -C "nr-softmodem|nr-uesoftmodem|nearRT-RIC" 5 1 | grep -E "Average|Média|PID"
   ```

6. Depois que os dois `iperf3` terminarem (60 s):

   ```bash
   wait
   tail -4 /tmp/iperf-ue1.log /tmp/iperf-ue2.log
   tail -20 ~/openairinterface5g/cmake_targets/ran_build/build/nrMAC_stats.log
   ```

Ao final, encerre UEs, gNB e RIC com Ctrl+C e remova os namespaces com `sudo ~/multi-ue-ns.sh down 2`.

Se a AMF recusar o NG Setup (`NGSetupFailure` no log do gNB) ou a sessão PDU de um slice (`reject` ou `S-NSSAI` no log da AMF ou do SMF), pare e guarde `docker compose logs oai-amf oai-smf > /tmp/core.log`.

### Resultados

A preencher com as saídas da linha de base.
