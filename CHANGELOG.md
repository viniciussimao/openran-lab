# Changelog

Todas as mudanças relevantes deste projeto são registradas aqui.

O formato segue o [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/), e o projeto adota o [Versionamento Semântico](https://semver.org/lang/pt-BR/). Cada etapa do [roadmap](docs/roadmap.md) gera uma release.

## [Não publicado]

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

[Não publicado]: https://github.com/viniciussimao/openran-lab/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/viniciussimao/openran-lab/releases/tag/v0.1.0
