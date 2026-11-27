# Registro de decisões

Decisões técnicas do projeto, com o contexto e o motivo de cada uma. Quando uma decisão mudar, atualize a seção correspondente e registre a mudança no [CHANGELOG](../CHANGELOG.md). Os detalhes de diagnóstico ficam em [troubleshooting.md](troubleshooting.md), o plano em [roadmap.md](roadmap.md) e a arquitetura em [architecture.md](architecture.md); aqui eles só são referenciados.

Índice:

1. [Repositório oficial do OAI](#1-repositório-oficial-do-oai)
2. [Versão fixa do OAI e commit da tag anotada](#2-versão-fixa-do-oai-e-commit-da-tag-anotada)
3. [FlexRIC como submódulo da tag do OAI](#3-flexric-como-submódulo-da-tag-do-oai)
4. [E2AP v3 e E2SM-KPM v3.00 nos dois lados](#4-e2ap-v3-e-e2sm-kpm-v300-nos-dois-lados)
5. [FlexRIC com gcc-12 e sem xApps multilinguagem](#5-flexric-com-gcc-12-e-sem-xapps-multilinguagem)
6. [Box generic/ubuntu2204 com host-passthrough](#6-box-genericubuntu2204-com-host-passthrough)
7. [Nomes fixos das VMs e rede configurável](#7-nomes-fixos-das-vms-e-rede-configurável)
8. [Correção de DNS e DNSSEC na role common](#8-correção-de-dns-e-dnssec-na-role-common)
9. [Core 5G do repositório do OAI, com imagens fixadas por digest](#9-core-5g-do-repositório-do-oai-com-imagens-fixadas-por-digest)
10. [ue.conf próprio para o nrUE](#10-ueconf-próprio-para-o-nrue)
11. [Agente E2 configurado por linha de comando](#11-agente-e2-configurado-por-linha-de-comando)
12. [Registros de idempotência em /var/lib/openran-lab/](#12-registros-de-idempotência-em-varlibopenran-lab)
13. [Saúde do core pelo NRF, não só pelo healthcheck](#13-saúde-do-core-pelo-nrf-não-só-pelo-healthcheck)
14. [Três cuidados na subida do cenário](#14-três-cuidados-na-subida-do-cenário)
15. [Y1 próprio, seguindo o Y1AP](#15-y1-próprio-seguindo-o-y1ap)
16. [Componentes prontos da O-RAN SC e terminação A1-P própria](#16-componentes-prontos-da-o-ran-sc-e-terminação-a1-p-própria)
17. [Etapa 8: comparação controlada entre Near-RT RICs](#17-etapa-8-comparação-controlada-entre-near-rt-rics)
18. [Preferência por E2SM-RC no controle de slice](#18-preferência-por-e2sm-rc-no-controle-de-slice)
19. [Observações do E2 na Etapa 1](#19-observações-do-e2-na-etapa-1)
20. [Escalonador de slices num fork próprio do OAI](#20-escalonador-de-slices-num-fork-próprio-do-oai)
21. [xApp de monitoração KPM por slice](#21-xapp-de-monitoração-kpm-por-slice)
22. [Correção do RIC do FlexRIC](#22-correção-do-ric-do-flexric)
23. [Fork do OAI como padrão](#23-fork-do-oai-como-padrão)
24. [Caminho O-RAN antes da exposição de dados](#24-caminho-o-ran-antes-da-exposição-de-dados)
25. [Controle de cotas pelo E2SM-RC estilo 2](#25-controle-de-cotas-pelo-e2sm-rc-estilo-2)
26. [Desenho da Etapa 3: caminho O-RAN com FlexRIC](#26-desenho-da-etapa-3-caminho-o-ran-com-flexric)

## 1. Repositório oficial do OAI

- **Contexto:** o código do OAI RAN aparece em mais de um lugar (GitHub do projeto Duranta e GitLab da EURECOM).
- **Decisão:** usar `https://github.com/duranta-project/openairinterface5g.git` (`oai_repo_url` em `ansible/group_vars/all.yml`).
- **Motivo:** é o repositório oficial; o GitLab da EURECOM é um espelho.
- **Consequências:** tags e commits são conferidos contra o GitHub. Um espelho atrasado não serve de referência.

## 2. Versão fixa do OAI e commit da tag anotada

- **Contexto:** o OAI publica tags semanais. A `2026.w46` é uma tag anotada: `refs/tags/2026.w46` aponta para o objeto da tag (`71a5b64c…`, `git cat-file -t` retorna `tag`), não para o commit.
- **Decisão:** fixar `oai_version: "2026.w46"` e `oai_commit: "cb98db5b5d2d95c86673821bfc9fa1be9e225b97"`, que é o **commit** da tag. Obtenha o commit com `git ls-remote <url> 'refs/tags/<tag>^{}'` ou, num clone, `git rev-parse '<tag>^{commit}'`.
- **Motivo:** reprodutibilidade. A role `oai_ran` confere o commit do clone e para se ele não bater; com o hash do objeto da tag, a asserção falha.
- **Consequências:** ao trocar a tag, atualize os dois valores e confira as decisões 3, 4, 5 e 9. Caso em [troubleshooting.md](troubleshooting.md) (seção "VMs").

## 3. FlexRIC como submódulo da tag do OAI

- **Contexto:** o FlexRIC tem repositório próprio, mas o OAI traz uma versão testada em `openair2/E2AP/flexric`.
- **Decisão:** usar o submódulo da tag fixa, iniciado com `git submodule update --init openair2/E2AP/flexric`.
- **Motivo:** o gNB e o nearRT-RIC ficam numa combinação testada junto pelo OAI.
- **Consequências:** a versão do FlexRIC muda só quando muda a tag do OAI. Desde a decisão 22, o submódulo vem do fork do OAI e aponta para o commit `ea6d9b4` do upstream do FlexRIC, 19 commits depois do que a tag fixa, por causa da correção do RIC para mais de uma xApp.

## 4. E2AP v3 e E2SM-KPM v3.00 nos dois lados

- **Contexto:** o agente E2 do OAI e o FlexRIC aceitam várias versões de E2AP e KPM, escolhidas na compilação.
- **Decisão:** `-DE2AP_VERSION=E2AP_V3 -DKPM_VERSION=KPM_V3_00` no OAI e no FlexRIC (`e2ap_version` e `kpm_version` em `group_vars/all.yml`).
- **Motivo:** versões diferentes nos dois lados quebram o E2 Setup ou a decodificação das indicações.
- **Consequências:** as duas roles (`oai_ran` e `flexric`) leem as mesmas variáveis, e mudar uma versão recompila os dois.

## 5. FlexRIC com gcc-12 e sem xApps multilinguagem

- **Contexto:** o FlexRIC da tag `2026.w46` recusa o gcc-11, padrão do Ubuntu 22.04 (`FlexRIC doesn't support gcc-11`). As xApps em Python dependem do SWIG.
- **Decisão:** a role `flexric` instala `gcc-12`/`g++-12` e roda o `cmake` com `CC=gcc-12 CXX=g++-12` num diretório `build` limpo, com `-DXAPP_MULTILANGUAGE=OFF`. O compilador padrão da VM **não** é trocado.
- **Motivo:** o OAI compila com o gcc-11; trocar o padrão afetaria o build do OAI. As xApps em C bastam e dispensam o SWIG.
- **Consequências:** o diretório `build` precisa ser limpo ao trocar de compilador, porque o CMake guarda o compilador no cache. Caso em [troubleshooting.md](troubleshooting.md) (seção "Compilação") e passo 3 do [guia da Etapa 1](stages/01-ran-core-e2.md).

## 6. Box generic/ubuntu2204 com host-passthrough

- **Contexto:** o OAI exige AVX2, e a CPU virtual padrão do libvirt pode escondê-lo.
- **Decisão:** box `generic/ubuntu2204` com `cpu_mode = "host-passthrough"` no `Vagrantfile`.
- **Motivo:** a VM enxerga as instruções da CPU do host (AVX2 e, se houver, AVX-512).
- **Consequências:** o host precisa ter AVX2 (o `scripts/preflight.sh` confere). Migração de VMs entre hosts diferentes não é suportada.

## 7. Nomes fixos das VMs e rede configurável

- **Contexto:** o servidor de teste é compartilhado, e a sub-rede padrão do vagrant-libvirt pode estar em uso.
- **Decisão:** no libvirt as VMs se chamam `openran-ran`, `openran-obs` e `openran-smo` (prefixo fixo no `Vagrantfile`). A rede de gerenciamento é configurável em `management_network` no `config.yml`.
- **Motivo:** nomes previsíveis facilitam a documentação e evitam colisão com outras VMs; a rede varia entre servidores e, por isso, não fica fixa no código.
- **Consequências:** veja "Nomes das VMs e rede" no [README](../README.md).

## 8. Correção de DNS e DNSSEC na role common

- **Contexto:** as boxes `generic/*` vêm com DNS públicos fixos e DNSSEC ligado, o que quebra a resolução de nomes atrás do dnsmasq do libvirt.
- **Decisão:** a role `common` aponta o DNS para o gateway da rede de gerenciamento e desliga DNSSEC e DNS over TLS.
- **Motivo:** sem isso, `apt`, `git clone` e `docker pull` falham dentro da VM.
- **Consequências:** não remova essa parte da role. Caso em [troubleshooting.md](troubleshooting.md) (seção "VMs").

## 9. Core 5G do repositório do OAI, com imagens fixadas por digest

- **Contexto:** o OAI traz o compose do core em `doc/tutorial_resources/oai-cn5g` (na tag fixa: OAI CN5G v2.2.2, 10 serviços). Algumas imagens usam tags móveis (`ims` e `trf-gen-cn5g` com `latest`).
- **Decisão:** a role `oai_cn5g` copia esses arquivos e gera um `docker-compose.override.yaml` com cada imagem no formato `imagem:tag@sha256:...`, a partir de `oai_cn5g_image_digests` (`group_vars/all.yml`).
- **Motivo:** o digest garante a mesma imagem em qualquer servidor; a tag fica só para leitura. Por isso as imagens `latest` do compose do OAI são aceitas.
- **Consequências:** ao trocar a tag do OAI, confira a lista de serviços e atualize os digests; a role para se faltar um serviço ou um digest. Conferência à mão: `docker compose config --images` (passo 4 do [guia da Etapa 1](stages/01-ran-core-e2.md)).

## 10. ue.conf próprio para o nrUE

- **Contexto:** com só `--uicc0.imsi`, o nrUE registra, mas não pede sessão PDU (`[SIM] no PDU sessions to request configured`). O `ue.conf` que vem com o OAI tem outro IMSI e um modelo de canal de satélite LEO.
- **Decisão:** manter `configs/ue.conf`, copiado pela role `oai_ran` para `~/ue.conf`, e rodar o nrUE com `-O ~/ue.conf`, sem `--uicc0.imsi`, e com a sintaxe nova do RFsimulator (`--rfsimulator.[0].serveraddr`). IMSI, chave e OPc são os valores de teste públicos do tutorial do CN5G.
- **Motivo:** o arquivo define a sessão PDU (DNN `oai`, SST 1), e a `oaitun_ue1` só sobe com ela.
- **Consequências:** na Etapa 1 o slice saiu como `1.ffffff` (SST 1, SD padrão), porque o arquivo só define `nssai_sst`. Na Etapa 2 (multi-slice), o SD deve ser explícito no UE, no gNB e no core. Caso em [troubleshooting.md](troubleshooting.md) (seção "Execução").

## 11. Agente E2 configurado por linha de comando

- **Contexto:** o gNB precisa saber o endereço do nearRT-RIC e onde estão os service models.
- **Decisão:** passar `--e2_agent.near_ric_ip_addr` e `--e2_agent.sm_dir /usr/local/lib/flexric/` na linha de comando do `nr-softmodem`, sem editar o arquivo de configuração do gNB.
- **Motivo:** o arquivo de configuração do OAI fica intacto, e o endereço do RIC pode mudar sem editar nada.
- **Consequências:** `/usr/local/lib/flexric/` é lida como pasta de plugins pelo gNB e pelo nearRT-RIC (veja a decisão 12).

## 12. Registros de idempotência em /var/lib/openran-lab/

- **Contexto:** as roles `oai_ran` e `flexric` guardam o commit e as opções do último build para não recompilar. As primeiras versões gravavam o registro do FlexRIC em `/usr/local/lib/flexric/`, e o nearRT-RIC abortou (`Not valid path for the SM plugins`), porque carrega todo arquivo dessa pasta como plugin.
- **Decisão:** os registros ficam em `/var/lib/openran-lab/` (`lab_state_dir`, criado pela role `common`): `oai_ran.build` e `flexric.build`. A role `flexric` falha se `/usr/local/lib/flexric/` tiver algo além de `.so`.
- **Motivo:** separar o estado do laboratório das pastas que o OAI e o FlexRIC leem.
- **Consequências:** apagar um registro força o build correspondente. Nunca grave outros arquivos em `/usr/local/lib/flexric/`. Caso em [troubleshooting.md](troubleshooting.md) (seção "Execução").

## 13. Saúde do core pelo NRF, não só pelo healthcheck

- **Contexto:** no teste de aceite da Etapa 1, o `oai-smf` v2.2.2 travou (log parado, sem heartbeats para o NRF nem PFCP com o UPF) e continuou `healthy`. A AMF não achou SMF (`SMF Selection, no SMF candidate is available`).
- **Decisão:** a verificação de saúde do core na automação da Etapa 7 confere o registro dos NFs no NRF (por exemplo, um NF discovery de SMF que retorne endereço válido), e não só o estado `healthy` do Docker.
- **Motivo:** o healthcheck das imagens do CN5G não detecta esse travamento.
- **Consequências:** à mão, compare a hora da última linha de `docker compose logs oai-smf` com a de `oai-amf`. Caso em [troubleshooting.md](troubleshooting.md) (seção "Execução").

## 14. Três cuidados na subida do cenário

- **Contexto:** nos testes da Etapa 1, a subida de RIC, gNB, UE e xApp falhou de três formas que não são defeito das roles.
- **Decisão:** o guia da Etapa 1 e o script de subida da Etapa 7 tratam os três casos:
  - depois de o core ficar `healthy`, esperar de 30 a 60 segundos (ou, melhor, até o NF discovery de SMF no NRF responder) antes de subir o gNB, porque o SMF ainda conclui o registro no NRF;
  - RIC, gNB e UE encerrados com Ctrl+C deixam o contexto do UE no core: se o UE não receber a sessão PDU, reiniciar o core (`docker compose down` e `docker compose up -d --wait`) e tentar de novo;
  - o nrUE leva de alguns segundos a mais de 20 para criar a `oaitun_ue1`: esperar a linha `TUN Interface oaitun_ue1 successfully configured` no log do UE, com timeout, e nunca usar um tempo fixo.
- **Motivo:** esperas fixas são frágeis e escondem falhas; condições observáveis tornam a subida reproduzível.
- **Consequências:** o comando de espera está no passo 5 do [guia da Etapa 1](stages/01-ran-core-e2.md); os sintomas, em [troubleshooting.md](troubleshooting.md) (seção "Execução").

## 15. Y1 próprio, seguindo o Y1AP

- **Contexto:** nenhum Near-RT RIC open source implementa a interface Y1.
- **Decisão:** escrever o produtor e o consumidor Y1, seguindo o Y1AP.
- **Motivo:** o Y1 é a saída de dados da RAN para o consumidor do slice, peça central do objetivo do projeto.
- **Consequências:** cada desvio da especificação fica documentado. Veja [architecture.md](architecture.md) e a Etapa 4 do [roadmap](roadmap.md).

## 16. Componentes prontos da O-RAN SC e terminação A1-P própria

- **Contexto:** o projeto prefere software pronto a código próprio. O FlexRIC não termina A1.
- **Decisão:**
  - O1: O1 Adapter do OAI (servidor NETCONF no gNB) e OAM da O-RAN SC (SDN-R/OpenDaylight e VES Collector). Nada de adaptador O1 próprio.
  - A1, lado Non-RT: A1 Policy Management Service e A1 Simulator da O-RAN SC.
  - A1, lado Near-RT: na Etapa 3, uma **terminação A1-P** própria (não um "adaptador") seguindo a OpenAPI do A1AP, testada contra o A1 Simulator e o A1 PMS.
- **Motivo:** menos código próprio e conformidade verificável: o A1 PMS da O-RAN SC opera a terminação como operaria qualquer Near-RT RIC.
- **Consequências:** tabela "Componentes prontos e componentes próprios" no [roadmap](roadmap.md) e "Decisões de projeto" em [architecture.md](architecture.md).

## 17. Etapa 8: comparação controlada entre Near-RT RICs

- **Contexto:** o projeto vai gerar dois artigos. O artigo 1 cobre as Etapas 1 a 7 (FlexRIC, terminação A1-P e Y1). O artigo 2 compara o FlexRIC com o Near-RT RIC da O-RAN SC (A1 Mediator nativo).
- **Decisão:** na Etapa 8 troca-se **só** o Near-RT RIC. Métricas, nomes das séries no Prometheus e scripts do experimento são independentes do Near-RT RIC, e a lógica da xApp de slicing fica separada da API do FlexRIC.
- **Motivo:** uma comparação controlada exige que todo o resto do cenário seja idêntico, e a xApp precisa ser portável.
- **Consequências:** "Publicações planejadas" e "Experimento final" no [roadmap](roadmap.md).

## 18. Preferência por E2SM-RC no controle de slice

- **Contexto:** o OAI expõe dois caminhos possíveis para controlar slices: o E2SM-RC (padrão O-RAN) e o SLICE SM próprio do FlexRIC. O gNB da tag fixa anuncia o E2SM-RC (decisão 19).
- **Decisão:** a xApp de slicing usa o E2SM-RC, se o controle de slice funcionar por ele no OAI `2026.w46`.
- **Motivo:** é o padrão que o Near-RT RIC da O-RAN SC também entende, o que facilita a Etapa 8.
- **Consequências:** o teste do controle de slice (E2SM-RC ou SLICE SM) abre a Etapa 2. Risco listado em "Riscos e decisões em aberto" no [roadmap](roadmap.md). A investigação do código mostrou que o OAI `2026.w46` não controla slices por nenhum service model; o caminho escolhido está na decisão 20.

## 19. Observações do E2 na Etapa 1

- **Contexto:** medições da validação manual da Etapa 1 no servidor de teste, com rfsim.
- **Decisão:** registrar as observações como base para as Etapas 2 e 8:
  - no E2 Setup, o gNB expõe os RAN functions E2SM-KPM, **E2SM-RC (ID 3)**, CCC e **SLICE_STATS_V0 (ID 145)**;
  - latência das indicações KPM no `xapp_kpm_moni`: cerca de 0,6 ms por UE e de 15 a 46 ms por célula.
- **Motivo:** confirmar o suporte a E2SM-RC (decisão 18) e ter uma linha de base para as medições do experimento.
- **Consequências:** os valores valem para rfsim no servidor de teste; não representam rádio real.

## 20. Escalonador de slices num fork próprio do OAI

**Data:** 2026-11-19. Substitui a versão "proposta" registrada com a investigação do PR #4.

- **Contexto:** a [investigação do controle de slice](stages/02-multi-slice-investigation.md), feita por leitura do código do OAI `2026.w46` e do FlexRIC do submódulo, mostrou que:
  - no E2SM-RC, o OAI implementa só o CONTROL estilo 1 (QoS flow mapping, que só faz `printf`) e o estilo 3 (handover); não há estilo 2 ("Radio Resource Allocation Control");
  - o E2SM-CCC e o SLICE aparecem no E2 Setup porque o agente anuncia todo plugin da pasta de service models, mas o OAI não implementa o controle de nenhum dos dois (o SLICE é um stub com dados aleatórios);
  - o escalonador MAC do gNB usa só proportional fair, sem cotas por slice e sem opção para trocar a política, embora cada candidato já traga a S-NSSAI do primeiro DRB.
- **Decisão:**
  - **Fork do OAI.** O escalonador por slice é um patch próprio no MAC do gNB, mantido no fork `https://github.com/viniciussimao/openairinterface5g`, na branch `slice-scheduler`, criada a partir do commit `cb98db5b` (tag `2026.w46`). O openran-lab só referencia o fork por URL e commit em `ansible/group_vars/all.yml`; nenhum código do OAI entra neste repositório.
  - **Licença.** O código do fork segue a licença do OAI, a Collaborative Standards Software License v1.0 (CSSL). Os arquivos novos levam o mesmo cabeçalho `SPDX-License-Identifier: LicenseRef-CSSL-1.0`. A CSSL dá licença de patentes gratuita para estudo, teste e pesquisa, e licença FRAND para outros usos. Contribuir o patch upstream exigiria assinar o ICLA da OpenAirInterface Software Alliance (OSA).
  - **Semântica das cotas.** Cotas por S-NSSAI (`sst` + `sd`) em porcentagem dos PRBs, alinhadas ao `RRMPolicyRatio` da 3GPP TS 28.541: `min_ratio`, `max_ratio` e `dedicated_ratio`. Dentro de cada slice, o proportional fair original continua valendo.
  - **Prova de conceito primeiro, sem E2.** Nesta fase as cotas são fixas, lidas de uma seção opcional do arquivo de configuração do gNB; sem a seção, o comportamento é idêntico ao upstream. A PoC cobre só o downlink.
  - **API interna pensada para o E2.** As cotas passam por uma API isolada num arquivo próprio do fork, que a configuração usa agora. Na fase seguinte, o handler do E2SM-RC (CONTROL estilo 2, ação 6 "Slice-level PRB quota", E2SM-RC v1.03, seção 7.6.3) só traduz os parâmetros para essa API.
  - **Near-RT RIC.** O FlexRIC continua sendo o Near-RT RIC principal. O Near-RT RIC da O-RAN SC fica para a Etapa 8 (decisão 17).
  - **xApp portável.** Na fase seguinte, a xApp de slicing tem a lógica de decisão separada da API do FlexRIC, numa camada própria, para ser portada para o Near-RT RIC da O-RAN SC. Essa separação segue a recomendação do tutorial da IEEE Communications Surveys & Tutorials (COMST) de 2026.
- **Motivo:** nenhum caminho nativo muda os PRBs de um slice nessa tag. Um fork fixado por commit mantém a regra "sem código de terceiros" no openran-lab e deixa o patch versionado e revisável, com um histórico que facilita o rebase em tags futuras. Começar sem E2 isola o risco: primeiro valida-se o escalonador; depois, o controle. O E2SM-RC mantém a decisão 18.
- **Consequências:**
  - o patch deve ser pequeno e localizado (arquivos novos mais um registro mínimo em arquivos existentes), para facilitar o rebase. Ao trocar a tag do OAI, a branch do fork é refeita sobre a tag nova e testada de novo;
  - o fork só vira o padrão em `group_vars` quando a PoC passar nos critérios de aceite do [guia da Etapa 2](stages/02-multi-slice.md); até lá, ele é ativado pela variável `oai_slice_scheduler_enabled`. Virou o padrão na [decisão 23](#23-fork-do-oai-como-padrão);
  - **resultado da PoC (2026-11-25):** o commit `125c80f` do fork passou nos testes T0 a T6. Sem cotas, o gNB se comporta como o upstream; com cotas, a divisão de PRBs segue o `max_ratio` (31,6 / 68,4 para 30 / 70), o `min_ratio` e o `dedicated_ratio`, sem custo de CPU mensurável e sem quedas em 10 minutos. Detalhes nos [resultados da PoC](stages/02-multi-slice-tests.md#resultados-da-poc);
  - a xApp de slicing monta a mensagem RC estilo 2 com os IDs de parâmetros da especificação, porque a biblioteca RC do FlexRIC não traz constantes para essa ação;
  - se a OAI publicar o código da demo de controle de slice por E2SM-RC mostrada com a VIAVI, comparar com o fork antes de escrever o handler E2;
  - o [NOTICE](../NOTICE) cita o fork e a licença CSSL.

## 21. xApp de monitoração KPM por slice

**Data:** 2026-11-25.

- **Contexto:** a `xapp_kpm_moni` do FlexRIC filtra os UEs pela S-NSSAI só com o SST, que o agente E2 do OAI trata como `1.ffffff`, e não vê os UEs dos slices `1.000001` e `1.000002`. A solução é assinar o REPORT estilo 4 uma vez por slice, com a S-NSSAI de 4 octetos. Ao ler o agente, apareceu outro problema: com duas assinaturas ao mesmo tempo, as medidas por UE saíam erradas, porque os contadores do relatório anterior ficavam em vetores globais indexados pela posição do UE em cada relatório.
- **Decisão:**
  - **Correção no fork.** O estado das medidas por UE passa a ser guardado por assinatura e por UE, num arquivo novo do agente E2 (`ran_func_kpm_state.{h,c}`), na branch `kpm-per-slice` do fork, sob a CSSL v1.0.
  - **xApp própria no openran-lab.** A `slice_kpm_monitor` fica em `xapps/slice_kpm_monitor/`, em C, sob a Apache-2.0, distribuída só como código-fonte. O núcleo (`skm_core`) não depende de RIC e tem testes; o adaptador FlexRIC é o único arquivo ligado à API do FlexRIC, seguindo a separação da decisão 20.
  - **Saída em JSON Lines**, uma linha por slice por relatório, para o produtor Y1 (Etapa 4) ler sem depender do formato de texto.
- **Motivo:** o KPM por slice é a base da xApp de slicing e do dashboard. Corrigir o agente no fork mantém a xApp simples e vale para qualquer xApp com mais de uma assinatura, inclusive as do FlexRIC. Com a xApp fora do FlexRIC, ela fica versionada com o laboratório e portável.
- **Consequências:**
  - o FlexRIC também está sob a CSSL v1.0. A xApp é código próprio sob a Apache-2.0, e quem compila a liga localmente ao FlexRIC. A compatibilidade para distribuir binários prontos não foi analisada; por isso, o openran-lab distribui só o código-fonte;
  - a compilação é manual até ser validada no servidor (critérios K1 a K4 no [guia da Etapa 2](stages/02-multi-slice.md#xapp-de-monitoração-kpm-por-slice)). Depois, uma role Ansible a automatiza, e `oai_fork_commit` passa a apontar para a branch `kpm-per-slice`;
  - a correção do agente pode ser proposta ao upstream, o que exigiria o ICLA da OSA (decisão 20).

## 22. Correção do RIC do FlexRIC

**Data:** 2026-11-25.

- **Contexto:** no K4 da xApp de KPM por slice, com duas xApps assinando o mesmo gNB, o cancelamento das assinaturas de uma apagava as da outra. A causa é um erro de uma linha no Near-RT RIC do FlexRIC (`eq_xapp_ric_gen_id`, em `src/ric/iApp/xapp_ric_id.c`), que compara o ID da xApp com ele mesmo. Detalhes no [guia da Etapa 2](stages/02-multi-slice.md#correção-no-ric-do-flexric).
- **Decisão:**
  - a correção foi validada à mão na VM (K4 passou com ela);
  - o upstream já tinha corrigido o erro, no commit `ea6d9b4` do FlexRIC (2026-11-13). A branch `kpm-per-slice` do fork do OAI aponta o submódulo para esse commit (commit `95a3e328` do fork). Não é preciso fork do FlexRIC nem patch no openran-lab, e nenhum código do FlexRIC (CSSL v1.0) entra neste repositório.
- **Motivo:** a Etapa 2 e as seguintes rodam mais de uma xApp ao mesmo tempo (a de KPM, a de slicing e as de teste). Sem a correção, qualquer xApp que termina derruba as assinaturas das outras. Usar o commit do upstream evita manter mais um fork.
- **Consequências:** o FlexRIC do laboratório fica 19 commits à frente do que a tag `2026.w46` do OAI fixa; entre eles, só documentação, CI, a `xapp_kpm_moni` e uma linha do TC SM. Ao trocar a tag do OAI, conferir se o submódulo novo já inclui o `ea6d9b4`.

## 23. Fork do OAI como padrão

**Data:** 2026-11-25.

- **Contexto:** a decisão 20 deixou o fork do OAI desligado em `group_vars` até a PoC passar. A PoC passou (T0 a T6), e a xApp de KPM por slice também (K1 a K4), com o fork e a correção do RIC (decisões 21 e 22).
- **Decisão:** `oai_slice_scheduler_enabled: true` por padrão, com `oai_fork_branch: kpm-per-slice` e `oai_fork_commit` no `95a3e328` (depois, `rc-slice-ctrl`, decisão 25). A role nova `xapps` compila a `slice_kpm_monitor` no provisionamento, depois das roles `oai_ran`, `flexric` e `oai_cn5g`.
- **Motivo:** a Etapa 2 depende do escalonador por slice, do KPM por assinatura e da correção do RIC. Com a chave desligada, cada teste exigia mudar a cópia local do `group_vars` e compilar a xApp à mão.
- **Consequências:**
  - um `vagrant provision` numa VM da Etapa 1 recompila o gNB e o FlexRIC;
  - voltar ao OAI upstream é pôr a chave em `false`: o gNB fica sem o escalonador por slice, sem o KPM por assinatura e sem a correção do RIC (a xApp continua compilando, mas o K4 falha).

## 24. Caminho O-RAN antes da exposição de dados

**Data:** 2026-11-26.

- **Contexto:** o roadmap previa, depois da Etapa 2, o Y1 (Etapa 3), a observabilidade (4) e o botão com o atalho (5), e só então o caminho O-RAN com SMO, Non-RT RIC, rApp, A1 e O1 (6). O mantenedor quer validar primeiro o cenário O-RAN completo, base do que o repositório vai sustentar.
- **Decisão:** o caminho O-RAN passa a ser a Etapa 3, logo depois da Etapa 2, e as etapas são renumeradas:

  | Etapa nova | Entrega | Etapa antiga |
  | --- | --- | --- |
  | 3 | Caminho O-RAN com FlexRIC | 6 |
  | 4 | Interface Y1 | 3 |
  | 5 | Observabilidade | 4 |
  | 6 | Botão de ação e atalho | 5 |

  As Etapas 1, 2, 7 e 8 não mudam, e cada release continua sendo `v0.N.0` para a Etapa N. Na Etapa 3, a entrada do laço é a API de intenção do rApp de SLA, chamada por linha de comando, e o efeito é verificado pela `slice_kpm_monitor` e pelo `iperf3`. Na Etapa 6, o botão do portal chama a mesma API, e o atalho entra para a comparação do artigo 1.
- **Motivo:** o laço intenção → rApp → A1 → xApp → E2 → gNB é o núcleo dos dois artigos e concentra os maiores riscos técnicos (terminação A1-P própria, SMO pesado, O1 do gNB, recursos do servidor com as VMs `ran` e `smo`). Y1, observabilidade e portal são camadas por cima do laço: expõem e acionam, mas não mudam o laço. Validá-lo antes evita refazer essas integrações.
- **Consequências:**
  - as referências às etapas em `roadmap.md`, `architecture.md`, no README e nas decisões 15, 16 e 21 seguem a numeração nova;
  - o rApp de SLA ganha uma API de intenção própria desde a Etapa 3, e o botão da Etapa 6 só a chama;
  - a xApp de slicing da Etapa 3 reaproveita o controle por E2SM-RC da Etapa 2, recebendo as cotas por A1 em vez da linha de comando.

## 25. Controle de cotas pelo E2SM-RC estilo 2

**Data:** 2026-11-26.

- **Contexto:** a decisão 20 previu um handler do E2SM-RC (CONTROL estilo 2, ação 6 "Slice-level PRB quota") sobre a API de cotas do escalonador por slice. Nem a tag `2026.w46` nem o `develop` do OAI (w47) têm esse estilo. O agente E2 do FlexRIC responde `CONTROL ACKNOWLEDGE` a todo CONTROL, sem caminho para `CONTROL FAILURE`.
- **Decisão:**
  - **Fork:** branch `rc-slice-ctrl` (a partir de `kpm-per-slice`). O gNB anuncia o estilo 2 só com a ação 6 e a árvore de parâmetros de 8.4.3.6, e só em nós com MAC. A conversão da mensagem fica num arquivo próprio (`ran_func_rc_slice.{h,c}`), sem depender do MAC, com testes GTest.
  - **Semântica:** a mensagem atualiza só os S-NSSAI que traz; os outros slices mantêm as cotas. A cota neutra (`min 0, max 100, ded 0`, os padrões do RRMPolicyRatio) remove a cota do slice; sem nenhum slice com cota, o DL volta à alocação padrão. Não há como remover todas de uma vez: um parâmetro LIST do E2SM-RC precisa de ao menos um item, e a lista vazia da primeira versão não passava no codificador ASN.1. O PLMN precisa ser de uma célula do gNB. O resultado passa pela validação do escalonador; uma mensagem inválida não muda nada e o motivo vai para o log (`[SLICE] E2SM-RC: ... recusada`).
  - **Resposta:** fica o `CONTROL ACKNOWLEDGE` do FlexRIC; a xApp confirma o efeito pelo log ou pelo KPM. Mudar o agente para responder `CONTROL FAILURE` exigiria mexer no FlexRIC e fica fora.
  - **xApp `slice_quota_ctrl`** em `xapps/slice_quota_ctrl/` (Apache-2.0): núcleo sem RIC, com testes, e adaptador FlexRIC, como a `slice_kpm_monitor`. É uma ferramenta de linha de comando; a xApp de slicing controlada por A1, da Etapa 3, reaproveita o núcleo e a montagem da mensagem.
- **Motivo:** atualizar só os slices da mensagem deixa uma xApp mexer num slice sem conhecer as cotas dos outros. A conversão separada do MAC pode ser testada sem o gNB. Manter o ACK do FlexRIC evita mais um fork.
- **Consequências:**
  - a xApp não sabe, pela resposta, se o gNB aplicou as cotas; os critérios C1 a C5 do [guia da Etapa 2](stages/02-multi-slice.md#controle-de-cotas-pelo-e2sm-rc) conferem pelo log e pelo KPM;
  - os critérios C1 a C5 passaram em 2026-11-27. `oai_fork_branch` passou para `rc-slice-ctrl` (commit `000b2090`), e a `slice_quota_ctrl` entrou em `xapps_list`;
  - os IDs dos parâmetros 1 a 12 seguem as implementações de referência e precisam ser conferidos com o texto do E2SM-RC v1.03.

## 26. Desenho da Etapa 3: caminho O-RAN com FlexRIC

**Data:** 2026-11-27.

- **Contexto:** a [investigação da Etapa 3](stages/03-oran-path.md) leu o código do A1 PMS (`ccsdk/oran`) e do A1 Simulator, a A1TD v5 (ETSI TS 103 988) e o repositório do OAM da O-RAN SC, para fechar o laço intenção → rApp → A1 → xApp → E2 com o mínimo de código próprio (decisão 16).
- **Decisão:**
  - **A1:** A1AP v2.0 (`/A1-P/v2`), a primeira versão que o A1 PMS tenta ao descobrir um Near-RT RIC;
  - **tipo de política:** `ORAN_SliceSLATarget_2.0.0`, o tipo padronizado de SLA de slice, com as metas de vazão DL por slice (`guaDlThptPerSlice` e `maxDlThptPerSlice`, em kbps). As outras metas recebem `NOT_ENFORCED`;
  - **xApp de SLA de slice:** em malha fechada, lê a vazão de cada slice pelo KPM e ajusta as cotas pelo E2SM-RC, com o núcleo separado da API do FlexRIC;
  - **terminação A1-P:** serviço próprio em Python na VM `ran`, separado da xApp, que valida as políticas contra o esquema da A1TD e as repassa à xApp no formato do A1 Mediator da O-RAN SC;
  - **Non-RT RIC:** A1 PMS e A1 Simulator da release N da O-RAN SC, em Docker, na VM `smo`, com as imagens fixadas por digest;
  - **rApp de SLA:** serviço próprio na VM `smo`, com uma API de intenção sobre a API northbound v3 do PMS; o gerenciador de rApps (R1) fica fora;
  - **O1:** módulo `o1` do telnet do OAI, O1 Adapter da OAI e OAM da O-RAN SC em Docker, no último passo da etapa;
  - **rede:** uma rede privada do libvirt entre as VMs, com IPs fixos no `config.yml`.
- **Motivo:** o tipo padronizado e a malha fechada fazem do laço o caso de uso de garantia de SLA de slice da O-RAN, que o PMS e o rApp de qualquer fornecedor entendem. Separar a terminação da xApp deixa a troca pelo A1 Mediator na Etapa 8 restrita à origem das políticas. Validar o PMS contra o simulador oficial antes da terminação própria dá uma referência de conformidade.
- **Consequências:**
  - a A1TD deixa para estudo futuro como o Near-RT RIC usa as metas de vazão por slice; a interpretação do laboratório (malha fechada sobre cotas de PRB) fica documentada no guia;
  - o OAM pede 16 GB e não cabe junto com a VM `ran` como está; os testes do O1 vão exigir ajustar a memória das VMs;
  - a etapa segue o plano de seis passos do guia, cada um com critério próprio e com a regra "manual primeiro, automação depois".
