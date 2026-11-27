# Etapa 2: testes e resultados

Roteiros, critérios e resultados de todos os testes da [Etapa 2](02-multi-slice.md). Os testes rodaram no servidor de teste (Ubuntu 24.04, 12 CPUs, 30 GB), na VM `ran` com 8 vCPU e 16 GB, com rfsim, entre 2026-11-23 e 2026-11-27. A forma de subir o cenário está em [Como executar](02-multi-slice.md#como-executar); aqui ficam só os passos próprios de cada teste.

| Grupo | O que valida | Situação |
| --- | --- | --- |
| [Linha de base](#linha-de-base-com-o-oai-upstream) | OAI upstream com dois slices, sem cotas | Referência |
| [T0 a T6](#poc-do-escalonador-t0-a-t6) | Escalonador por slice com cotas fixas | Passou |
| [K1 a K4](#kpm-por-slice-k1-a-k4) | KPM por slice (`slice_kpm_monitor`) | Passou |
| [C1 a C5](#controle-pelo-e2sm-rc-c1-a-c5) | Controle das cotas pela E2 (`slice_quota_ctrl`) | Passou |

## Observações que valem para todos os testes

- **No rfsim, a simulação roda mais rápido que o relógio** (cerca de 2,4 vezes com os dois nrUE no ar). Os Mbit/s do iperf3 são de relógio de parede e não se comparam com a capacidade teórica da célula. As porcentagens de PRBs não são afetadas.
- **Com os dois UEs saturados, a CPU da VM fica perto do limite** (93%). A vazão pode ser limitada pela CPU, por isso a divisão de PRBs é medida pelo log do escalonador, e a vazão é um dado complementar.
- **Compare vazões só entre cenários com os mesmos processos no ar:** um nrUE ligado, mesmo ocioso, disputa a CPU com o outro.

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

A linha de base rodou com o OAI da tag `2026.w46`, sem o fork. Para repeti-la hoje, ponha `oai_slice_scheduler_enabled: false` na cópia local de `ansible/group_vars/all.yml` (e desfaça depois com `git checkout ansible/group_vars/all.yml`). No servidor de teste, atualize o repositório e provisione duas vezes. Na segunda, o esperado é `changed=0`.

```bash
cd ~/vinicius/openran-lab && git pull
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

## PoC do escalonador (T0 a T6)

Testes do escalonador por slice com as cotas fixas no `.conf` do gNB, antes do controle pela E2. O desenho, aprovado em 2026-11-19, está em [Escalonador por slice](02-multi-slice.md#escalonador-por-slice). Os testes usaram o gNB do fork na branch `slice-scheduler` (commit `125c80f`), que tinha só o escalonador. Na época, o fork era ligado só na cópia local do servidor (`oai_slice_scheduler_enabled: true`); desde a [decisão 23](../decisions.md#23-fork-do-oai-como-padrão), ele é o padrão. Os perfis de cotas usados estão em [Perfis de cotas](02-multi-slice.md#perfis-de-cotas).

### Critérios de aceite

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

## KPM por slice (K1 a K4)

Testes da [`slice_kpm_monitor`](02-multi-slice.md#xapp-de-monitoração-kpm-por-slice) contra o log do escalonador e o iperf3. Nos testes com tráfego, a xApp roda num segundo terminal enquanto a `janela` gera o tráfego no primeiro.

### Critérios de aceite

| Teste | Cenário | Critério |
| --- | --- | --- |
| K1 Separação | Perfil `30-70`, os dois UEs conectados | Uma linha por slice por segundo, cada uma com o UE dele (`--per-ue`) |
| K2 PRBs | Perfil `30-70`, os dois saturados (`janela k2 60 200M 1 2`) | `prb_dl_pct` de cada slice, na média, a até 3 p.p. do `%disp` do `slice-prb-share.sh`, com as duas medições no mesmo intervalo |
| K3 Vazão | Perfil `30-70`, os dois sem saturação (`janela k3b 60 20M 1 2`) | `thp_dl_kbps` de cada slice a até 5% da vazão do iperf3 do UE. Com saturação, o KPM mostra o que o RLC do gNB transmite e fica acima do iperf3 |
| K4 Convivência | K2 com a `xapp_kpm_moni` rodando junto | Os números da `slice_kpm_monitor` não mudam; as duas xApps terminam sem erro; nenhum assert no gNB. Exige a [correção do RIC](02-multi-slice.md#correção-no-ric-do-flexric) |

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

## Controle pelo E2SM-RC (C1 a C5)

Testes do [controle de cotas pelo E2SM-RC](02-multi-slice.md#controle-de-cotas-pelo-e2sm-rc). Nos testes com tráfego, são três terminais: a `janela` no A, a `slice_kpm_monitor` no B e a `slice_quota_ctrl` no C, que manda o CONTROL no meio do tráfego.

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
