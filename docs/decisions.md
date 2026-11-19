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
20. [Controle de slice por E2SM-RC com patch próprio no OAI (proposta)](#20-controle-de-slice-por-e2sm-rc-com-patch-próprio-no-oai-proposta)

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
- **Consequências:** a versão do FlexRIC muda só quando muda a tag do OAI.

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
- **Consequências:** cada desvio da especificação fica documentado. Veja [architecture.md](architecture.md) e a Etapa 3 do [roadmap](roadmap.md).

## 16. Componentes prontos da O-RAN SC e terminação A1-P própria

- **Contexto:** o projeto prefere software pronto a código próprio. O FlexRIC não termina A1.
- **Decisão:**
  - O1: O1 Adapter do OAI (servidor NETCONF no gNB) e OAM da O-RAN SC (SDN-R/OpenDaylight e VES Collector). Nada de adaptador O1 próprio.
  - A1, lado Non-RT: A1 Policy Management Service e A1 Simulator da O-RAN SC.
  - A1, lado Near-RT: na Etapa 6, uma **terminação A1-P** própria (não um "adaptador") seguindo a OpenAPI do A1AP, testada contra o A1 Simulator e o A1 PMS.
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
- **Consequências:** o teste do controle de slice (E2SM-RC ou SLICE SM) abre a Etapa 2. Risco listado em "Riscos e decisões em aberto" no [roadmap](roadmap.md). A investigação do código mostrou que o OAI `2026.w46` não controla slices por nenhum service model; o caminho proposto está na decisão 20.

## 19. Observações do E2 na Etapa 1

- **Contexto:** medições da validação manual da Etapa 1 no servidor de teste, com rfsim.
- **Decisão:** registrar as observações como base para as Etapas 2 e 8:
  - no E2 Setup, o gNB expõe os RAN functions E2SM-KPM, **E2SM-RC (ID 3)**, CCC e **SLICE_STATS_V0 (ID 145)**;
  - latência das indicações KPM no `xapp_kpm_moni`: cerca de 0,6 ms por UE e de 15 a 46 ms por célula.
- **Motivo:** confirmar o suporte a E2SM-RC (decisão 18) e ter uma linha de base para as medições do experimento.
- **Consequências:** os valores valem para rfsim no servidor de teste; não representam rádio real.

## 20. Controle de slice por E2SM-RC com patch próprio no OAI (proposta)

**Status:** proposta, até a validação no servidor descrita em "A confirmar no servidor" no [guia da Etapa 2](stages/02-multi-slice.md#5-a-confirmar-no-servidor).

- **Contexto:** a [investigação do controle de slice](stages/02-multi-slice.md#investigação-controle-de-slice), feita por leitura do código do OAI `2026.w46` e do FlexRIC do submódulo, mostrou que:
  - no E2SM-RC, o OAI implementa só o CONTROL estilo 1 (QoS flow mapping, que só faz `printf`) e o estilo 3 (handover); não há estilo 2 ("Radio Resource Allocation Control");
  - o E2SM-CCC e o SLICE aparecem no E2 Setup porque o agente anuncia todo plugin da pasta de service models, mas o OAI não implementa o controle de nenhum dos dois (o SLICE é um stub com dados aleatórios);
  - o escalonador MAC do gNB usa só proportional fair, sem cotas por slice e sem opção para trocar a política, embora cada candidato já traga a S-NSSAI do primeiro DRB.
- **Decisão:** escrever um patch próprio para o OAI, aplicado pela role `oai_ran` no provisionamento depois de validado à mão, com:
  - uma política de alocação de PRBs por slice no MAC (`dl_rb_alloc` e depois `ul_rb_alloc`), com cotas mínima, máxima e dedicada por S-NSSAI (modelo `RRMPolicyRatio` da 3GPP TS 28.541) e proportional fair dentro de cada slice;
  - o CONTROL estilo 2 do E2SM-RC, ação "Slice-level PRB quota" (E2SM-RC v1.03, seção 7.6.3), anunciado no E2 Setup e ligado a essa política.
- **Motivo:** é o único caminho que de fato muda os PRBs de um slice nessa tag. O E2SM-RC mantém a decisão 18 (portabilidade para o Near-RT RIC da O-RAN SC na Etapa 8). O E2SM-CCC (`O-RRMPolicyRatio`) fica como alternativa, porque usa o mesmo trabalho no MAC e só muda o adaptador E2. O SLICE SM foi descartado: exigiria o mesmo patch e não é padrão.
- **Consequências:**
  - o projeto passa a manter um patch sobre o código do OAI, que continua sendo baixado no provisionamento. Ao trocar a tag do OAI, o patch precisa ser reaplicado e testado;
  - antes de versionar o patch, confirmar que ele é compatível com a regra "sem código de terceiros" e com a licença do OAI (`LicenseRef-CSSL-1.0`): um diff carrega linhas de contexto do OAI. Alternativas: um arquivo novo próprio para a política, mais um diff mínimo de registro em `main.c` e no agente E2, ou um fork do OAI fixado por commit;
  - a xApp de slicing monta a mensagem RC estilo 2 com os IDs de parâmetros da especificação, porque a biblioteca RC do FlexRIC não traz constantes para essa ação;
  - se a OAI publicar o código da demo de controle de slice por E2SM-RC mostrada com a VIAVI, reavaliar esta decisão antes de escrever o patch;
  - confiança: alta de que não há caminho nativo; média quanto ao esforço do patch no MAC e ao suporte do estilo 2 na biblioteca RC do FlexRIC.
