# Etapa 2: multi-slice

**Status:** concluída (release `v0.2.0`, 2026-11-27).

**Objetivo:** dois slices (S-NSSAI) no core e no gNB, um UE em cada, e uma xApp que altera a partição de recursos entre eles.

**Critério de aceite:** mudar a cota de um slice muda a vazão medida com `iperf3` (veja o [roadmap](../roadmap.md)). Passou no teste C2: com as cotas trocadas de 30/70 para 70/30 pela E2, o UE 1 foi de 38 para 75 Mbit/s e o UE 2, de 74 para 38 Mbit/s.

Este guia tem três partes:

- **este arquivo:** o que a etapa entrega, como executar o cenário e como cada componente funciona;
- **[testes e resultados](02-multi-slice-tests.md):** roteiros, critérios e resultados de todos os testes (linha de base, T0 a T6, K1 a K4 e C1 a C5);
- **[investigação](02-multi-slice-investigation.md):** a leitura do código do OAI que mostrou que nenhum caminho nativo controla os PRBs por slice e levou ao fork (decisão 20).

## O que a etapa entrega

```
VM ran
├── OAI CN5G (Docker): AMF, SMF e UPF com os slices 1.000001 e 1.000002
├── gNB do fork do OAI (nr-softmodem, --rfsim)
│   ├── escalonador DL por slice (cotas min/max/dedicated, RRMPolicyRatio)
│   └── agente E2: KPM por assinatura e E2SM-RC CONTROL estilo 2 (cotas)
├── nrUE 1 (namespace ue1, slice 1.000001) e nrUE 2 (namespace ue2, slice 1.000002)
├── nearRT-RIC (FlexRIC ea6d9b4)
└── xApps: slice_kpm_monitor (KPM por slice) e slice_quota_ctrl (muda as cotas)
```

| Componente | Onde | O que faz | Decisão |
| --- | --- | --- | --- |
| Dois slices no core e no gNB | `lab_slices` em `ansible/group_vars/all.yml`; roles `oai_cn5g` e `oai_ran` | Fonte única dos slices `1.000001` e `1.000002`; o core e o `.conf` do gNB são gerados a partir dela | — |
| Fork do OAI | [`viniciussimao/openairinterface5g`](https://github.com/viniciussimao/openairinterface5g), branch `rc-slice-ctrl`, commit `000b2090` | Escalonador DL por slice, KPM por assinatura no agente E2 e CONTROL estilo 2 do E2SM-RC. Fixado em `oai_fork_commit` | 20, 23 e 25 |
| FlexRIC | Submódulo do fork, commit `ea6d9b4` do upstream | Near-RT RIC com a correção para mais de uma xApp | 22 |
| [`slice_kpm_monitor`](../../xapps/slice_kpm_monitor/) | `xapps/`, Apache-2.0; role `xapps` | PRBs e vazão por slice, a cada segundo, em texto e JSON Lines | 21 |
| [`slice_quota_ctrl`](../../xapps/slice_quota_ctrl/) | `xapps/`, Apache-2.0; role `xapps` | Muda ou remove as cotas de um slice pela E2 | 25 |
| `multi-ue-ns.sh` e `slice-prb-share.sh` | `scripts/`, copiados para a home na VM | Namespaces dos dois nrUE; resumo do log do escalonador | — |

Tudo isso é provisionado pelo `vagrant up` (ou `vagrant provision`). Subir o cenário e rodar o tráfego é manual, como na Etapa 1.

## Como executar

### 1. Provisionar

No servidor, a partir da pasta do repositório:

```bash
vagrant up ran            # VM nova: cria, provisiona e compila tudo (mais de uma hora)
vagrant provision ran     # VM existente: aplica as mudanças do repositório
vagrant ssh ran -c "which slice_kpm_monitor slice_quota_ctrl; git -C ~/openairinterface5g log -1 --oneline"
```

O esperado: as duas xApps em `/usr/local/bin` e o OAI no commit do fork (`000b2090`).

Com `oai_slice_scheduler_enabled: true` (o padrão), a role `oai_ran` põe o clone do OAI no commit do fork (`oai_fork_commit`) e recompila. Ela só faz isso com o clone sem mudanças locais: se você editou algo em `~/openairinterface5g` na VM, desfaça antes de provisionar. Com a chave em `false`, o clone volta ao commit da tag `2026.w46`, sem o escalonador por slice e sem o controle pela E2.

### 2. Subir o cenário

Use seis terminais na VM (`vagrant ssh ran` em cada um) ou janelas do `tmux`. Espere cada passo terminar antes do próximo.

**Terminal 1: core.** Reiniciar o core evita o contexto antigo dos UEs (decisão 14). Espere o SMF se registrar no NRF:

```bash
cd ~/oai-cn5g && docker compose down && docker compose up -d --wait && sleep 45 && docker compose logs oai-smf 2>&1 | grep -a "NF Instance Registration" | tail -n 2
```

O esperado: uma linha de registro sem `could not get response from NRF`.

**Terminal 2: nearRT-RIC.**

```bash
cd ~/openairinterface5g/openair2/E2AP/flexric && ./build/examples/ric/nearRT-RIC 2>&1 | tee /tmp/ric.log
```

**Terminal 3: gNB.** O perfil de cotas vem da variável `CONF`, que vale só no terminal onde foi definida: defina-a **neste** terminal. Os perfis estão na tabela da seção [Perfis de cotas](#perfis-de-cotas).

```bash
CONF=~/gnb-slices-30-70.conf
cd ~/openairinterface5g/cmake_targets/ran_build/build && sudo ./nr-softmodem -O "$CONF" --gNBs.[0].min_rxtxtime 6 --rfsim \
  --e2_agent.near_ric_ip_addr "127.0.0.1" --e2_agent.sm_dir "/usr/local/lib/flexric/" 2>&1 | tee /tmp/gnb.log
```

No terminal 2 deve aparecer `Registered E2 nodes = 1`.

**Terminal 4: namespaces e UE 1.**

```bash
sudo ~/multi-ue-ns.sh up 2
cd ~/openairinterface5g/cmake_targets/ran_build/build && sudo ip netns exec ue1 ./nr-uesoftmodem -O ~/ue-slice1.conf -r 106 --numerology 1 --band 78 -C 3619200000 --rfsim \
  --rfsimulator.[0].serveraddr 10.201.1.100 2>&1 | tee /tmp/ue1.log
```

**Terminal 5: UE 2**, depois de o UE 1 ter a interface:

```bash
timeout 90 tail -n +1 -f /tmp/ue1.log | grep -a -m1 "oaitun_ue1 successfully configured"
cd ~/openairinterface5g/cmake_targets/ran_build/build && sudo ip netns exec ue2 ./nr-uesoftmodem -O ~/ue-slice2.conf -r 106 --numerology 1 --band 78 -C 3619200000 --rfsim \
  --rfsimulator.[0].serveraddr 10.202.1.100 2>&1 | tee /tmp/ue2.log
```

Cada nrUE cria a própria `oaitun_ue1` dentro do seu namespace: `ip a` na VM não a mostra.

**Terminal 6 (A): testes.** Defina as duas funções de apoio (valem enquanto o terminal estiver aberto):

```bash
ue_ip() { sudo ip netns exec "ue$1" ip -4 -o addr show oaitun_ue1 | awk '{print $4}' | cut -d/ -f1; }
janela() { local nome=$1 dur=$2 rate=$3 L n pids=(); shift 3; L=$(wc -l < /tmp/gnb.log); for n in "$@"; do sudo ip netns exec "ue$n" iperf3 -c 192.168.70.135 -p "520$n" -B "$(ue_ip "$n")" -u -b "$rate" -l 1300 -R -t "$dur" --logfile "/tmp/iperf-$nome-ue$n.log" & pids+=($!); done; wait "${pids[@]}"; for n in "$@"; do echo "== UE$n"; tail -n 4 "/tmp/iperf-$nome-ue$n.log"; done; tail -n +"$L" /tmp/gnb.log | ~/slice-prb-share.sh -s 3 | tee "/tmp/$nome.txt"; }
```

`ue_ip N` mostra o IP do UE N. `janela NOME SEGUNDOS TAXA UE...` roda o iperf3 UDP DL nos UEs pedidos ao mesmo tempo e, no fim, resume com o `slice-prb-share.sh` só o trecho do log do gNB que corresponde ao teste.

Depois, espere o UE 2, suba o segundo servidor do iperf3 e ligue um ping de fundo em cada UE. Sem tráfego por alguns minutos, a rede libera a conexão do UE, e o nrUE desta versão não consegue voltar ([troubleshooting](../troubleshooting.md#execução)):

```bash
for i in $(seq 45); do ue_ip 2 | grep -q . && break; sleep 2; done; ue_ip 1; ue_ip 2
docker exec oai-ext-dn iperf3 -s -p 5202 -D
for n in 1 2; do sudo ip netns exec ue$n ping -I oaitun_ue1 -i 1 192.168.70.135 > /dev/null 2>&1 & done; sleep 3; jobs
grep -a "cota DL" /tmp/gnb.log; grep -a -c -E "Assert|Segmentation" /tmp/gnb.log
```

O esperado: dois IPs (`10.0.0.x`), os dois pings em `Running`, as cotas do perfil e `0` asserts.

### 3. Experimentar

Use mais dois terminais na VM: o **B**, para medir, e o **C**, para mudar as cotas.

1. **Tráfego (terminal A):** os dois UEs saturados por 60 s.

   ```bash
   janela demo 60 200M 1 2
   ```

2. **Medição por slice (terminal B, uns 5 s depois):** PRBs e vazão de cada slice, a cada segundo.

   ```bash
   XAPP_DURATION=50 slice_kpm_monitor --slice 1.000001 --slice 1.000002
   ```

3. **Troca de cotas (terminal C, uns 25 s depois do passo 1):** inverte a divisão para 70/30 sem reiniciar nada.

   ```bash
   slice_quota_ctrl --quota 1.000001:0:70:0 --quota 1.000002:0:30:0; grep -a -E "E2SM-RC|cota DL" /tmp/gnb.log | tail -n 3
   ```

   No terminal B, o `prb_dl_pct` dos dois slices troca de lado (cerca de 22% e 52% para 52% e 22%) em até dois relatórios. Para remover as cotas e voltar à divisão sem limites: `slice_quota_ctrl --remove 1.000001 --remove 1.000002`.

O agente E2 do FlexRIC responde `CONTROL ACKNOWLEDGE` mesmo quando o gNB recusa as cotas; o resultado fica no log do gNB (linhas `[SLICE] E2SM-RC: ... aplicada` ou `recusada: <motivo>`).

### 4. Encerrar

Encerre com Ctrl+C, nesta ordem: UE 2, UE 1, gNB e nearRT-RIC. Depois, remova os namespaces com `sudo ~/multi-ue-ns.sh down 2`. As cotas mudadas pela E2 valem até o gNB reiniciar; na subida, ele lê de novo o perfil do `CONF`.

## Escalonador por slice

Fica no fork do OAI ([decisão 20](../decisions.md#20-escalonador-de-slices-num-fork-próprio-do-oai)). Cobre só o downlink. As cotas vêm de uma seção opcional do `.conf` do gNB na subida e podem ser trocadas depois pela E2 ([controle de cotas](#controle-de-cotas-pelo-e2sm-rc)). Sem cotas, o gNB usa o proportional fair do OAI sem nenhuma mudança (teste T0).

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

As cotas passam por uma API própria (`nr_slice_policy_set()` e `nr_slice_policy_get()`), que valida a lista e troca as cotas de uma vez sob o `sched_lock`. A configuração do gNB usa essa API na subida, e o handler do E2SM-RC (CONTROL estilo 2, ação 6) a usa para trocar as cotas com o gNB no ar.

O gNB escreve no log, cerca de uma vez por segundo, os PRBs DL usados por slice e por UE, separando transmissões novas e retransmissões, e os PRBs que ficaram sem uso no período.

### Limitações conhecidas

- **Só vale a S-NSSAI do primeiro DRB do UE.** Um UE com sessões PDU em dois slices é contado no slice da primeira.
- **Uma retransmissão pode passar um pouco do orçamento do slice.** A retransmissão HARQ precisa do mesmo número de PRBs da transmissão original.
- **Um slice com orçamento menor que 5 PRBs só atende retransmissões.** O proportional fair do OAI não aloca menos de 5 PRBs para dados novos.
- **As cotas valem por slot, não como média numa janela.** O `RRMPolicyRatio` não define a janela; aqui cada slot respeita as cotas.

### Perfis de cotas

A role `oai_ran` gera um `.conf` do gNB por perfil de cotas, a partir do `~/gnb-slices.conf` (que tem só os dois slices, sem cotas), inserindo a seção `slice_policies` dentro de `MACRLCs`. Os perfis ficam em `oai_ran_gnb_quota_profiles`, nos defaults da role.

| Arquivo | Cotas (slice 1 / slice 2) | Testes |
| --- | --- | --- |
| `~/gnb-slices.conf` | sem `slice_policies` | T0 |
| `~/gnb-slices-30-70.conf` | `max_ratio` 30 / 70 | T1, T2, T5, T6 |
| `~/gnb-slices-min60.conf` | `min_ratio` 60 / 0, `max_ratio` 100 / 100 | T3 |
| `~/gnb-slices-ded40.conf` | `dedicated_ratio` e `min_ratio` 40 / 0, `max_ratio` 100 / 100 | T4 |
| `~/gnb-slices-100-100.conf` | `max_ratio` 100 / 100 | controle: escalonador ligado, sem limite |

### Como ler o log do escalonador

O gNB escreve no log, cerca de uma vez por segundo de tempo simulado, uma linha `[SLICE]` por período com os PRBs DL usados por slice e por UE (transmissões novas e retransmissões) e os PRBs que ficaram sem uso. O `slice-prb-share.sh` (copiado para `~/slice-prb-share.sh`) soma os períodos de um trecho do log e mostra duas porcentagens por slice:

- **`%disp`:** a fatia dos PRBs livres para PDSCH, somados slot a slot. Compare-a com as cotas sabendo que um UE sozinho usa no máximo cerca de 75% dos PRBs disponíveis ([resultados da PoC](02-multi-slice-tests.md#resultados-da-poc)): com `max_ratio` 30, o slice fica em cerca de 22% (30% de 75%);
- **`%usados`:** a fatia dos PRBs efetivamente alocados, que serve para a divisão entre slices que disputam.

## xApp de monitoração KPM por slice

Primeira peça da xApp de slicing ([decisão 21](../decisions.md#21-xapp-de-monitoração-kpm-por-slice)). A `slice_kpm_monitor` assina, para cada slice, o REPORT estilo 4 do E2SM-KPM com a S-NSSAI de 4 octetos (SST + SD). A cada segundo, soma por slice as medidas por UE `RRU.PrbTotDl`, `RRU.PrbTotUl`, `DRB.UEThpDl` e `DRB.UEThpUl`. O código fica em [`xapps/slice_kpm_monitor/`](../../xapps/slice_kpm_monitor/), sob a Apache-2.0:

| Parte | Arquivo | Papel |
| --- | --- | --- |
| Núcleo | `src/skm_core.{h,c}` | S-NSSAI, soma por slice, descarte do primeiro relatório de cada UE, saída em texto e JSON Lines. Não depende de RIC; tem testes em `tests/` |
| Adaptador FlexRIC | `src/skm_flexric.c` | Conecta pela E42, monta as assinaturas e converte as indicações para o núcleo. Portar para outro Near-RT RIC é trocar só este arquivo |

### Uso

```bash
XAPP_DURATION=60 slice_kpm_monitor --slice 1.000001 --slice 1.000002 --json /tmp/skm.jsonl --per-ue
```

A saída tem uma linha por slice por relatório, por exemplo:

```
slice 1.000001  UEs 1 (novos 0)  prb_dl_pct 22.0  prb_ul_pct 1.0  thp_dl_kbps 39100.0  thp_ul_kbps 120.0
```

O JSON Lines (`--json`) tem os mesmos campos, mais o início da coleta em `ts_us`. É o formato que o produtor Y1 da Etapa 4 vai ler. A xApp acrescenta ao fim do arquivo: apague-o antes de cada teste (`rm -f /tmp/skm.jsonl`), ou as médias misturam rodadas. Um slice sem UE não gera linha, porque o agente E2 não envia indicação vazia.

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

### Compilação

Desde a validação (C1 a C5), o provisionamento faz tudo: `oai_fork_commit` aponta para o commit `000b2090` da branch `rc-slice-ctrl`, e a role `xapps` compila a `slice_quota_ctrl` junto com a `slice_kpm_monitor` e põe as duas no `PATH`. Para compilar à mão, por exemplo ao mexer no código direto na VM, os comandos são os mesmos da [`slice_kpm_monitor`](#compilação-na-vm), trocando o nome da pasta.

## Testes e resultados

Os roteiros completos e todos os números estão em [testes e resultados](02-multi-slice-tests.md). Todos os testes rodaram no servidor de teste, na VM `ran` com 8 vCPU e 16 GB, com rfsim.

| Grupo | O que valida | Resultado |
| --- | --- | --- |
| [Linha de base](02-multi-slice-tests.md#linha-de-base-com-o-oai-upstream) | Core com dois slices, dois UEs e CPU da VM, com o OAI upstream | Dois UEs registrados nos slices certos; sem cotas, divisão de 49/51; CPU da VM em 93% com os dois saturados |
| [T0 a T6](02-multi-slice-tests.md#poc-do-escalonador-t0-a-t6) | Escalonador por slice com cotas fixas no `.conf` | Passou: 31,6/68,4 dos PRBs usados para cotas de 30/70; mínimo e dedicado respeitados; 10 min sem quedas |
| [K1 a K4](02-multi-slice-tests.md#kpm-por-slice-k1-a-k4) | `slice_kpm_monitor` contra o log do escalonador e o iperf3 | Passou: PRBs a até 2,6 p.p. do escalonador, vazão a 1% a 2% do iperf3 sem saturação, duas xApps juntas sem erro |
| [C1 a C5](02-multi-slice-tests.md#controle-pelo-e2sm-rc-c1-a-c5) | Troca, recusa e remoção de cotas pela E2 | Passou: troca de 30/70 para 70/30 em até dois relatórios, recusa pelo gNB, remoção com 38/38 dos PRBs |

## Pontos em aberto

- **Os cerca de 25% de PRBs que nenhum UE usa.** Candidatos: slots em que o UE não pode ser escalonado (processos HARQ esperando confirmação, restrições de `k1`), o slot misto do TDD contado como cheio, ou PRBs livres no `vrb_map` que o proportional fair não aproveita. Investigar com o `nrMAC_stats.log` lido durante o tráfego e, se preciso, um contador por slot no log do escalonador.
- **Fork como padrão.** Ligado em 2026-11-25, depois da PoC e da [xApp de monitoração KPM por slice](#xapp-de-monitoração-kpm-por-slice) ([decisão 23](../decisions.md#23-fork-do-oai-como-padrão)).
- **Controle pela E2.** Pronto: o [controle de cotas pelo E2SM-RC](#controle-de-cotas-pelo-e2sm-rc) passou nos critérios C1 a C5.
