# Changelog

Todas as mudanças relevantes deste projeto são registradas aqui.

O formato segue o [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/), e o projeto adota o [Versionamento Semântico](https://semver.org/lang/pt-BR/). Cada etapa do [roadmap](docs/roadmap.md) gera uma release.

## [Não publicado]

### Adicionado

- Guia da Etapa 3 (`docs/stages/03-oran-path.md`) com a investigação e o desenho proposto do caminho O-RAN: A1AP v2.0 (como o A1 PMS descobre e usa o Near-RT RIC, lido no código do `ccsdk/oran`), tipo de política `ORAN_SliceSLATarget_2.0.0` da A1TD v5, xApp de SLA de slice em malha fechada, terminação A1-P própria, A1 PMS e A1 Simulator da release N em Docker, rApp de SLA com API de intenção, O1 com o módulo `o1` do telnet do OAI, o O1 Adapter e o OAM da O-RAN SC em Docker, rede privada entre as VMs, recursos e plano em seis passos. Desenho aprovado na decisão 26. README com a Etapa 3 em andamento.

### Alterado

- Documentação revisada para quem chega ao projeto: o guia da Etapa 2 foi dividido em três arquivos (como executar e como funciona; testes e resultados; investigação), com um roteiro único e atualizado para subir o cenário e usar as xApps. O README ganhou o cenário atual, um guia de leitura, a tabela de testes e resultados e as versões do fork e do FlexRIC. Atualizados o NOTICE (branch `rc-slice-ctrl`), os comentários das VMs no `config.example.yml` (numeração nova das etapas), a decisão 3, a arquitetura, o guia da Etapa 1 (gNB do fork) e o CONTRIBUTING (branches `release/`). Novo `xapps/README.md`. Links internos conferidos.

## [0.2.0] - 2026-11-27

Etapa 2 concluída: dois slices (`1.000001` e `1.000002`) no core e no gNB, um UE em cada; escalonador DL por slice com cotas no modelo `RRMPolicyRatio` num fork do OAI (branch `rc-slice-ctrl`, commit `000b2090`, a partir da tag `2026.w46`); KPM por slice e controle das cotas pela E2 (E2SM-RC CONTROL estilo 2, ação 6), com as xApps próprias `slice_kpm_monitor` e `slice_quota_ctrl`; FlexRIC no commit `ea6d9b4` do upstream. Critério de aceite: mudar a cota de um slice muda a vazão medida com o `iperf3` (C2: com as cotas de 30/70 trocadas para 70/30, o UE 1 foi de 38 para 75 Mbit/s e o UE 2, de 74 para 38 Mbit/s). Validado no servidor de teste com dois `vagrant provision` (o segundo com `changed=0`) e os critérios T0 a T6, K1 a K4 e C1 a C5 do guia da Etapa 2.

### Adicionado

- xApp `slice_quota_ctrl` (`xapps/slice_quota_ctrl/`, Apache-2.0): muda as cotas de PRB por slice com o CONTROL estilo 2 do E2SM-RC, ação 6 "Slice-level PRB quota". Núcleo sem RIC, com testes, e adaptador FlexRIC; `--check` mostra o anúncio do gNB e `--remove` tira a cota de um slice. Guia da Etapa 2 com o handler, os critérios C1 a C5 e os resultados; decisão 25.
- Fork do OAI, branch `rc-slice-ctrl` (commit `000b2090`): o gNB anuncia e trata o CONTROL estilo 2, ação 6, aplicando as cotas com `nr_slice_policy_set()`; a cota neutra (`min 0, max 100, ded 0`) remove a cota do slice. A conversão da mensagem tem testes GTest, sem depender do MAC.
- Role `xapps`: copia para a VM, compila contra o FlexRIC do submódulo, testa (`ctest`) e põe no `PATH` as xApps de `xapps/`, começando pela `slice_kpm_monitor`. Recompila quando o código da xApp ou o commit do FlexRIC mudam.
- Correção de uma linha no Near-RT RIC do FlexRIC (`eq_xapp_ric_gen_id`), validada à mão: sem ela, o cancelamento das assinaturas de uma xApp apagava as de outra. Guia da Etapa 2 com o passo manual e os resultados dos testes K1 a K4 da `slice_kpm_monitor`; decisão 22; caso novo no troubleshooting. Critérios K2 (mesmo intervalo) e K3 (sem saturação) revistos; K1 a K4 passaram, com o K2b e o K3b.
- xApp `slice_kpm_monitor` (`xapps/slice_kpm_monitor/`, Apache-2.0): assina o REPORT estilo 4 do E2SM-KPM uma vez por slice, com a S-NSSAI de 4 octetos, e escreve por slice, a cada segundo, `RRU.PrbTotDl/Ul` e `DRB.UEThpDl/Ul` em texto e JSON Lines. Núcleo sem dependência de RIC, com testes, e adaptador FlexRIC separado. Liga a `libe42_xapp_shared` do FlexRIC com a `libasn1_nr_rrc`, que traz o runtime ASN.1 que falta nela. Guia da Etapa 2 com compilação manual e critérios K1 a K4; decisão 21.
- Fork do OAI, branch `kpm-per-slice` (commit `12537539`): o agente E2 guarda o estado das medidas KPM por UE por assinatura, o que corrige os valores com mais de uma assinatura ao mesmo tempo.
- Etapa 2, linha de base com dois slices (`1.000001` e `1.000002`, em `lab_slices` no `group_vars`): a role `oai_cn5g` gera `conf/config.lab.yaml` a partir do `config.yaml` do OAI, só inserindo os slices na AMF, no SMF e no UPF, e o override do compose o monta no lugar do original; a role `oai_ran` gera `~/gnb-slices.conf` a partir do `.conf` do gNB da Etapa 1, trocando só a `snssaiList`, e copia `configs/ue-slice1.conf`, `configs/ue-slice2.conf` e `scripts/multi-ue-ns.sh` para a VM.
- Etapa 2, integração do escalonador por slice: `oai_slice_scheduler_enabled` (desligado por padrão até a PoC passar), `oai_fork_repo_url`, `oai_fork_branch` e `oai_fork_commit` no `group_vars`. Ligada, a role `oai_ran` põe o clone do OAI no commit do fork (só com o clone limpo) e recompila; desligada, volta ao commit da tag. Ligada, a role também gera `~/gnb-slices-<perfil>.conf` para os perfis de cotas de `oai_ran_gnb_quota_profiles` (`30-70`, `min60` e `ded40`, dos testes T1 a T6).
- Guia da Etapa 2: roteiro e resultados dos testes da PoC. O escalonador passou nos testes T0 a T6 (31,6 / 68,4 dos PRBs para cotas de 30 / 70, mínimo e dedicados respeitados, 10 minutos sem quedas, KPM de célula recebido, sem custo de CPU mensurável). Os controles mostram o teto prático de um UE (75% dos PRBs disponíveis) e a simulação do rfsim cerca de 2,4 vezes mais rápida que o relógio. A decisão 20 registra o resultado; o troubleshooting ganhou dois casos dos testes.
- Perfil de cotas `100-100` (controle, sem limite) em `oai_ran_gnb_quota_profiles`.
- `scripts/slice-prb-share.sh`: resume as linhas `[SLICE]` do log do gNB num trecho do teste (PRBs DL por slice, em % dos disponíveis e dos usados, com novas e retransmissões).
- `scripts/multi-ue-ns.sh`: cria e remove os network namespaces dos nrUE (um por UE), sem mexer no iptables.
- Guia da Etapa 2: desenho aprovado do escalonador por slice (cotas no modelo `RRMPolicyRatio`, algoritmo, API, log), limitações conhecidas e roteiro da linha de base com o OAI upstream.
- Pacote `sysstat` (`mpstat`, `pidstat`) na role `common`, para medir CPU por processo.
- Guia da Etapa 2: resultados da rodada 1 da linha de base (dois slices registrados, ping 5/5 nos dois UEs, 95,5 Mbit/s com um UE e divisão 49/51 com dois, memória e CPU da VM) e roteiro refeito, com as medições num comando único durante o tráfego. Solução de problemas: a `oaitun_ue1` dos nrUE fica dentro do namespace de cada um.
- Guia da Etapa 2: rodada 2 da linha de base, com NG Setup anunciando os dois slices, divisão 49/51 sem cotas, CPU da VM em 93% com os dois UEs saturados e memória por processo. Registra que a `xapp_kpm_moni` não reporta UEs com SD explícito (filtro S-NSSAI `1.ffffff` no agente E2 do OAI); a solução também entrou no troubleshooting.
- Guia da Etapa 2: critérios de aceite T0 a T6 da PoC, com saturação UDP de `-b 100M` por UE e o T6 restrito ao KPM de célula; o KPM por UE e por slice fica para a xApp de monitoração própria, feita depois da PoC.
- Fork do OAI em `viniciussimao/openairinterface5g` (branch `slice-scheduler`, a partir da tag `2026.w46`) para o escalonador por slice da Etapa 2. O NOTICE cita o fork e a licença CSSL v1.0.
- Guia da Etapa 2 (`docs/stages/02-multi-slice.md`) com a investigação do controle de slice no OAI `2026.w46`: o que o CONTROL do E2SM-RC, do E2SM-CCC e do SLICE faz no agente E2, como o escalonador MAC trata slices, quais xApps de controle do FlexRIC são compatíveis, recomendação e testes a fazer no servidor.
- Decisão 20 em `docs/decisions.md` (proposta): controle de slice por E2SM-RC estilo 2 com patch próprio no OAI (política de PRBs por slice no MAC).

### Alterado

- Controle de cotas automatizado: `oai_fork_branch` passa para `rc-slice-ctrl` (commit `000b2090`), com o CONTROL estilo 2 do E2SM-RC, e a `slice_quota_ctrl` entra em `xapps_list`. Os critérios C1 a C5 passaram: troca de cotas com tráfego em até 2 relatórios, recusa pelo gNB, remoção das cotas (38% / 38% dos PRBs) e convivência com a `slice_kpm_monitor`.
- Roadmap: o caminho O-RAN completo (SMO, Non-RT RIC, rApp de SLA, terminação A1-P, O1) passa a ser a Etapa 3, logo depois da Etapa 2; Y1, observabilidade e botão com atalho viram as Etapas 4, 5 e 6. Na Etapa 3, a entrada do laço é a API de intenção do rApp. Referências atualizadas no README, na arquitetura e nas decisões; decisão 24; índice das decisões completado até a 24.
- Fork do OAI ligado por padrão (`oai_slice_scheduler_enabled: true`), na branch `kpm-per-slice`, commit `95a3e328`: escalonador por slice, KPM por assinatura e o submódulo do FlexRIC no `ea6d9b4`, em que o upstream corrigiu o erro do RIC com duas xApps. A correção manual do RIC sai do guia; decisões 22 (revista) e 23. Validado no servidor: dois provisionamentos (o segundo com `changed=0`) e o K4 repetido.
- README: o início rápido clona `https://github.com/viniciussimao/openran-lab.git` em vez do marcador `<seu-usuario>`.
- Decisão 20 reescrita como decisão tomada (2026-11-19): escalonador por slice num fork próprio do OAI sob a CSSL v1.0, cotas no modelo `RRMPolicyRatio`, prova de conceito com cotas fixas no `.conf` do gNB e sem E2, API interna pronta para o E2SM-RC estilo 2, FlexRIC como Near-RT RIC principal e xApp de slicing com a lógica separada da API do FlexRIC.
- README: Etapa 2 em andamento. Roadmap: risco do controle de slice atualizado com o resultado da investigação. Decisão 18 aponta para a decisão 20.
- Contexto para agentes de IA reorganizado: as decisões técnicas saem do `CLAUDE.md` para o novo `docs/decisions.md` (contexto, decisão, motivo e consequências de cada uma); convenções, fluxo de trabalho e verificações vão para o novo `AGENTS.md`, neutro de ferramenta; o `CLAUDE.md` só importa o `AGENTS.md`; o contexto pessoal do mantenedor vai para um `CLAUDE.local.md` fora do Git (`.gitignore`). README e CONTRIBUTING.md apontam para os novos arquivos.

### Corrigido

- Guia da Etapa 2: a função `janela` espera só os próprios iperf3, para funcionar com pings de fundo no mesmo terminal. Dois casos novos no troubleshooting: UE que cai por inatividade e `janela` que não termina.
- Guia da Etapa 2: dois parágrafos tinham o texto inteiro da decisão 23 no lugar do link para ela.

## [0.1.0] - 2026-11-17

Etapa 1 concluída: RAN + core 5G + E2 numa VM, com OAI `2026.w46` (commit `cb98db5b`), FlexRIC do submódulo da tag (gcc-12), OAI CN5G v2.2.2 e E2AP v3/KPM v3.00. Testado com libvirt 10.0.0 e ansible-core 2.21.5.

### Adicionado

- Roles `oai_ran`, `flexric` e `oai_cn5g` validadas no servidor de teste: dois `vagrant provision` (o segundo com `changed=0`) e um `vagrant destroy`/`vagrant up` do zero, seguido do critério de aceite (`ping` 5/5 e `xapp_kpm_moni`).
- Solução de problemas e guia da Etapa 1: espera de 30 a 60 segundos entre o core ficar `healthy` e a subida do gNB (SMF ainda se registrando no NRF); reinício do core quando o UE deixa de receber a sessão PDU depois de várias reinicializações com Ctrl+C; espera pela linha `TUN Interface oaitun_ue1 successfully configured` no log do UE (`| tee /tmp/ue.log` e `timeout 90 tail -f ... | grep -m1`) em vez de um tempo fixo. CLAUDE.md: o script de subida da Etapa 7 deve tratar os três casos.
- Estrutura inicial do repositório: README, licença Apache-2.0, guia de contribuição e política de segurança.
- `Vagrantfile` configurável por `config.yml` (VMs, recursos e rede de gerenciamento), com VMs nomeadas `openran-ran`, `openran-obs` e `openran-smo`.
- Roles Ansible `common` (DNS, disco e pacotes base) e `docker`, validadas no servidor de teste em 2026-11-17: `vagrant up ran` terminou com `failed=0`, o disco da VM foi estendido de 62 para 124 GB, o Docker 29.10.0 foi instalado e o DNS ficou em `192.168.121.1` (gateway da rede de gerenciamento).
- `scripts/preflight.sh` para verificar o host antes do `vagrant up`.
- README: versões testadas do libvirt (10.0.0) e do Ansible (ansible-core 2.21.5) no host de teste.
- Documentação: arquitetura, roadmap, solução de problemas e guia manual da Etapa 1.
- CI com yamllint, ansible-lint, shellcheck e verificação de sintaxe do `Vagrantfile`.
- Roles Ansible da Etapa 1, habilitadas para o grupo `ran` no `site.yml`:
  - `oai_ran`: clona o OAI na tag `2026.w46` (conferindo o commit), inicia o submódulo do FlexRIC, roda `build_oai -I` e compila gNB e nrUE com o agente E2; copia `configs/ue.conf` para a home do `lab_user`;
  - `flexric`: instala o gcc-12 e compila e instala o FlexRIC do submódulo com `CC=gcc-12 CXX=g++-12`;
  - `oai_cn5g`: copia os arquivos do core do repositório do OAI, fixa todas as imagens por digest (`imagem:tag@sha256:...`) num `docker-compose.override.yaml` e sobe o core com `docker compose up --wait`. OAI CN5G v2.2.2, com os 10 serviços do compose da tag (`oai-amf`, `oai-smf`, `oai-upf`, `oai-nrf`, `oai-ausf`, `oai-udm`, `oai-udr`, `ims`, `oai-ext-dn` e `mysql` 9.6) fixados pelos digests validados no servidor de teste.

  Os builds são idempotentes (registro do commit e das opções de cada build) e usam `async`/`poll`. Novas variáveis em `group_vars/all.yml`: `lab_home`, `oai_src_dir` e `oai_cn5g_image_digests`.
- `configs/ue.conf`: configuração do nrUE (IMSI, chaves de teste do tutorial do CN5G e sessão PDU no DNN `oai`, SST 1), copiada para `~/ue.conf` pela role `oai_ran`.
- Solução de problemas: UE que registra, mas não pede sessão PDU (`[SIM] no PDU sessions to request configured`).
- CONTRIBUTING.md e CLAUDE.md: fluxo com o `main` protegido (só por pull request, merge liberado com os dois jobs do CI verdes; correções de CI no mesmo branch).
- Solução de problemas: sessão PDU que não sobe porque o `oai-smf` v2.2.2 travou e continuou `healthy` (`SMF Selection, no SMF candidate is available` na AMF). CLAUDE.md: a verificação de saúde do core na Etapa 7 deve consultar o NRF, não só o healthcheck dos containers.
- Etapa 1 validada à mão no servidor de teste: `ping` do UE até o ext-dn com 5/5 respostas, E2 Setup aceito com os service models KPM, RC, CCC e SLICE_STATS, e `xapp_kpm_moni` mostrando métricas.
- Roadmap: Etapa 8 (Near-RT RIC da O-RAN SC, para comparação com o FlexRIC), tabela de componentes prontos e próprios, e publicações planejadas.

### Alterado

- Guia da Etapa 1: o nrUE passa a rodar com `-O ~/ue.conf` e sem `--uicc0.imsi`. Sem o arquivo, o UE não pede sessão PDU e a `oaitun_ue1` não é criada; com ele, o UE recebeu o IP `10.0.0.2` e o `ping` até o ext-dn deu 5/5.
- Guia da Etapa 1: tabela do que é automático (passos 1 a 4) e do que continua manual (subir RIC, gNB, UE e xApp, e validar).
- Guia da Etapa 1: o FlexRIC passa a ser compilado com gcc-12 (`CC=gcc-12 CXX=g++-12`, num diretório `build` limpo), porque o da tag `2026.w46` recusa o gcc-11 do Ubuntu 22.04. O OAI continua com o gcc-11. O caso foi incluído em `docs/troubleshooting.md`.
- Etapa 6: o lado Near-RT do A1 passa a ser uma terminação A1-P seguindo a OpenAPI do A1AP; O1 usa componentes prontos (O1 Adapter do OAI e OAM da O-RAN SC).

### Corrigido

- Role `flexric`: o registro de idempotência era gravado em `/usr/local/lib/flexric/`, de onde o nearRT-RIC e o agente E2 carregam todo arquivo como plugin; o nearRT-RIC abortava (`Not valid path for the SM plugins`). Os registros de `flexric` e `oai_ran` passam para `/var/lib/openran-lab/` (`lab_state_dir`, criado pela role `common`). Os registros antigos são aproveitados uma vez e apagados, para VMs já provisionadas se corrigirem sem recompilar. A role `flexric` agora falha se `/usr/local/lib/flexric/` tiver algo além de `.so`. O caso foi incluído em `docs/troubleshooting.md`.
- `oai_commit` passa a ser `cb98db5b5d2d95c86673821bfc9fa1be9e225b97`, o commit da tag `2026.w46`. O valor anterior (`71a5b64c…`) era o hash do objeto da tag anotada, e a asserção da role `oai_ran` parou o provisionamento. README e CLAUDE.md corrigidos; o caso foi incluído em `docs/troubleshooting.md`.

[Não publicado]: https://github.com/viniciussimao/openran-lab/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/viniciussimao/openran-lab/releases/tag/v0.2.0
[0.1.0]: https://github.com/viniciussimao/openran-lab/releases/tag/v0.1.0
