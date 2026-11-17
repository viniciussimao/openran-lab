# CLAUDE.md

Contexto do projeto para o Claude Code. Leia este arquivo inteiro antes de qualquer tarefa.

## O que é o projeto

O `openran-lab` é um laboratório OpenRAN reproduzível e público, feito para que outros profissionais subam o mesmo cenário nos próprios servidores. Ele usa:

- OAI RAN (projeto Duranta): gNB e nrUE com RF simulado (`--rfsim`);
- OAI CN5G: core 5G em Docker;
- FlexRIC: Near-RT RIC e xApps;
- VMs KVM criadas pelo Vagrant (provider libvirt) e provisionadas pelo Ansible.

**Objetivo final:** o consumidor de um slice acompanha dados da RAN num dashboard (interface Y1 + OpenTelemetry + Grafana) e, com um botão, pede mais banda. O pedido segue o caminho O-RAN: SMO → rApp → A1 → xApp → E2 → gNB. O plano completo está em `docs/roadmap.md`, e a arquitetura em `docs/architecture.md`.

## Ambiente de trabalho

- **Você (Claude Code) roda no notebook Windows do mantenedor.** Lá não há KVM, Vagrant funcional nem Ansible. Você edita, revisa, faz commits e abre PRs.
- **Os testes rodam num servidor Linux compartilhado** (Ubuntu 24.04, 12 CPUs, 30 GB de RAM), acessado pelo mantenedor via SSH. Você **não** executa comandos nesse servidor. Quando algo precisar ser testado, diga exatamente quais comandos o mantenedor deve rodar lá, e ele cola a saída de volta.
- Fluxo: notebook (edição) → GitHub → servidor (`git pull` + `vagrant up` / `vagrant provision`).
- Os arquivos usam finais de linha LF (`.gitattributes`). Nunca introduza CRLF.

## Convenções obrigatórias

| Item | Idioma |
| --- | --- |
| Nomes de arquivos, pastas, variáveis, roles, funções | Inglês |
| `README.md`, `docs/`, comentários no código, nomes das tasks Ansible | Português |
| Mensagens de commit | Inglês, Conventional Commits (`feat:`, `fix:`, `docs:`, `chore:`) |

- Ansible: módulos com nome completo (`ansible.builtin.*`), toda task com `name`, variáveis de role com o prefixo da role (`common_`, `docker_`, `oai_ran_`...).
- Nada que varie entre servidores fica fixo no código: vai para o `config.yml` (modelo em `config.example.yml`).
- Versões sempre fixas em `ansible/group_vars/all.yml`. Nunca use `latest` nem branches móveis.
- O repositório não inclui código de terceiros. OAI, CN5G e FlexRIC são baixados no provisionamento.
- Sem segredos, chaves ou certificados no Git.
- **Git e pull requests.** O `main` é protegido por um ruleset no GitHub: não aceita push direto, push forçado nem exclusão; toda mudança entra por pull request (0 aprovações exigidas, porque há um só mantenedor), e o merge só é liberado se os dois jobs do CI passarem (`yamllint e ansible-lint` e `shellcheck e Vagrantfile`). Por isso:
  - nunca faça commit nem push no `main`. Toda mudança, inclusive de documentação, vai num branch `feat/`, `fix/` ou `docs/` (veja o `CONTRIBUTING.md`) com PR;
  - nos branches de trabalho, envie cada commit para o PR logo depois de commitar, sem perguntar;
  - se o CI falhar num PR, corrija no mesmo branch.

## Princípios do roadmap

1. **Manual primeiro, automação depois:** um passo só vira role Ansible depois de validado à mão no servidor e documentado em `docs/stages/`.
2. **Cada etapa tem critério de aceite** (em `docs/roadmap.md`). Etapa concluída gera uma release: `v0.1.0` = Etapa 1, `v0.2.0` = Etapa 2, até `v1.0.0`.
3. **Nomes fixos das VMs:** no libvirt as VMs se chamam `openran-ran`, `openran-obs` e `openran-smo` (prefixo fixo no `Vagrantfile`). A rede de gerenciamento é configurável no `config.yml`, caso a sub-rede padrão esteja em uso no servidor.

## Decisões técnicas já tomadas

- Repositório oficial do OAI: `https://github.com/duranta-project/openairinterface5g.git`. O GitLab da EURECOM é um espelho.
- Versão fixa: tag `2026.w46` (commit `cb98db5b5d2d95c86673821bfc9fa1be9e225b97`). O FlexRIC usado é o submódulo dessa tag (`openair2/E2AP/flexric`).
- Tags anotadas: o `oai_commit` é o hash do **commit** da tag, não o do objeto da tag. Obtenha-o com `git ls-remote <url> 'refs/tags/<tag>^{}'` ou `git rev-parse '<tag>^{commit}'`. O `refs/tags/<tag>` de uma tag anotada aponta para o objeto da tag (`git cat-file -t` retorna `tag`); na `2026.w46` esse objeto é `71a5b64c…`. A role `oai_ran` confere o commit do clone e para se ele não bater.
- E2AP v3 e E2SM-KPM v3.00 nos dois lados (`-DE2AP_VERSION=E2AP_V3 -DKPM_VERSION=KPM_V3_00`).
- FlexRIC compilado com `-DXAPP_MULTILANGUAGE=OFF` (xApps em C, sem SWIG).
- FlexRIC compilado com gcc-12: o da tag `2026.w46` recusa o gcc-11, padrão do Ubuntu 22.04 (`FlexRIC doesn't support gcc-11`). A futura role `flexric` deve instalar `gcc-12`/`g++-12`, rodar o `cmake` com `CC=gcc-12 CXX=g++-12` num diretório `build` limpo e **não** trocar o compilador padrão da VM, porque o OAI compila com o gcc-11.
- VMs: box `generic/ubuntu2204`, `cpu_mode = host-passthrough` (o OAI precisa de AVX2).
- As boxes `generic/*` vêm com DNS fixo e DNSSEC ligado, o que quebra a resolução de nomes atrás do dnsmasq do libvirt. A role `common` corrige isso; não remova.
- Arquivos do CN5G: `doc/tutorial_resources/oai-cn5g` do próprio repositório do OAI (na tag fixa: OAI CN5G v2.2.2, 10 serviços).
- Imagens do CN5G fixadas por digest em `oai_cn5g_image_digests` (`group_vars/all.yml`), no formato `imagem:tag@sha256:...`, aplicadas por um `docker-compose.override.yaml` gerado pela role `oai_cn5g`. Imagens com tag `latest` no compose do OAI (`ims`, `trf-gen-cn5g`) são aceitas só porque o digest é o que vale. Ao trocar a tag do OAI, confira os serviços e atualize os digests.
- Sintaxe nova do nrUE: `--rfsimulator.[0].serveraddr`.
- O nrUE roda com `-O ~/ue.conf` (de `configs/ue.conf`, copiado pela role `oai_ran`) e **sem** `--uicc0.imsi`. Sem o arquivo, o UE registra mas não pede sessão PDU (`[SIM] no PDU sessions to request configured`) e a `oaitun_ue1` não sobe. O `ue.conf` que vem com o OAI não serve: tem outro IMSI e um modelo de canal de satélite LEO. IMSI, chave e OPc são os valores de teste públicos do tutorial do CN5G.
- Saúde do core: o healthcheck dos containers do CN5G não basta. No teste de aceite da Etapa 1, o `oai-smf` v2.2.2 travou (log parado, sem heartbeats para o NRF nem PFCP com o UPF) e continuou `healthy`; a AMF não achou SMF (`no SMF candidate is available`). Na automação da Etapa 7, a verificação de saúde do core deve conferir o registro dos NFs no NRF (por exemplo, um NF discovery de SMF que retorne endereço válido), e não só o `healthy` do Docker. Caso em `docs/troubleshooting.md`.
- Na Etapa 1 o slice do UE saiu como `1.ffffff` (SST 1, SD padrão), porque o `ue.conf` só define `nssai_sst`. Na Etapa 2 (multi-slice), o SD deve ser explícito no UE, no gNB e no core.
- Agente E2 configurado por linha de comando: `--e2_agent.near_ric_ip_addr` e `--e2_agent.sm_dir /usr/local/lib/flexric/`.
- Nenhum Near-RT RIC open source implementa Y1: o produtor e o consumidor Y1 serão próprios, seguindo o Y1AP, com os desvios documentados.
- **Pronto antes de próprio.** Use software existente sempre que houver (tabela em `docs/roadmap.md`):
  - O1: O1 Adapter do OAI (servidor NETCONF no gNB) + OAM da O-RAN SC (SDN-R/OpenDaylight e VES Collector). Não escreva adaptador O1.
  - A1, lado Non-RT: A1 Policy Management Service e A1 Simulator da O-RAN SC.
  - A1, lado Near-RT: o FlexRIC não termina A1. Na Etapa 6, escreva uma **terminação A1-P** (não "adaptador") seguindo a OpenAPI do A1AP, para que o A1 PMS da O-RAN SC a opere como um Near-RT RIC qualquer. Teste contra o A1 Simulator e o A1 PMS.
- **Etapa 8 e artigos:** o projeto vai gerar dois artigos. O artigo 1 cobre as Etapas 1 a 7 (FlexRIC + terminação A1-P + Y1). O artigo 2 compara o FlexRIC com o Near-RT RIC da O-RAN SC (A1 Mediator nativo), trocando **só** o Near-RT RIC. Por isso:
  - mantenha métricas, nomes de séries no Prometheus e scripts do experimento independentes do Near-RT RIC;
  - escreva a lógica da xApp de slicing separada da API do FlexRIC, para facilitar o porte;
  - prefira E2SM-RC ao SLICE SM próprio do FlexRIC, se o OAI suportar, porque é o padrão que o outro RIC também entende.

## Estado atual

- Etapa 1 (RAN + core + E2) validada à mão no servidor: `ping` do UE 5/5, E2 Setup aceito com KPM/RC/CCC/SLICE_STATS, `xapp_kpm_moni` OK. Guia em `docs/stages/01-ran-core-e2.md`.
- Roles `common` (DNS, disco, pacotes) e `docker` validadas no servidor de teste em 2026-11-17: `vagrant up ran` com `failed=0`, disco estendido de 62 para 124 GB, Docker 29.10.0 e DNS em `192.168.121.1`.
- Roles `oai_ran`, `flexric` e `oai_cn5g` escritas (passos 1 a 4 do guia) e habilitadas para o grupo `ran`; **validação no servidor pendente**. Subir RIC, gNB, UE e xApp continua manual. A Etapa 1 só fecha (release `v0.1.0`) depois de as roles passarem num `vagrant up` do zero e o critério de aceite ser repetido.
- Medições da validação manual (servidor de teste, rfsim):
  - o gNB expõe, no E2 Setup, os RAN functions E2SM-KPM, **E2SM-RC (ID 3)**, CCC e **SLICE_STATS_V0 (ID 145)**. Como o OAI suporta E2SM-RC, a xApp de slicing deve usá-lo (veja "Etapa 8 e artigos");
  - latência das indicações KPM observada no `xapp_kpm_moni`: cerca de 0,6 ms por UE e de 15 a 46 ms por célula.

## Ao concluir uma tarefa ou etapa

1. Atualize a documentação afetada (`README.md`, `docs/`).
2. Registre a mudança no `CHANGELOG.md`, na seção "Não publicado".
3. Ao fechar uma etapa: atualize a tabela de status do README, mova as entradas do CHANGELOG para a nova versão e prepare a tag da release.
4. Diga ao mantenedor quais comandos rodar no servidor para validar.

## Verificações

Antes de commitar, confira sintaxe e lint quando as ferramentas estiverem disponíveis no notebook. O CI do GitHub roda as mesmas verificações:

```bash
yamllint .
ansible-lint ansible/
shellcheck scripts/*.sh
ruby -c Vagrantfile
```
