# Changelog

Todas as mudanças relevantes deste projeto são registradas aqui.

O formato segue o [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/), e o projeto adota o [Versionamento Semântico](https://semver.org/lang/pt-BR/). Cada etapa do [roadmap](docs/roadmap.md) gera uma release.

## [Não publicado]

### Adicionado

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
  - `oai_cn5g`: copia os arquivos do core do repositório do OAI, fixa todas as imagens por digest num `docker-compose.override.yaml` e sobe o core com `docker compose up --wait`.

  Os builds são idempotentes (registro do commit e das opções de cada build) e usam `async`/`poll`. Novas variáveis em `group_vars/all.yml`: `lab_home`, `oai_src_dir` e `oai_cn5g_image_digests`.
- Etapa 1 validada à mão no servidor de teste: `ping` do UE até o ext-dn com 5/5 respostas, E2 Setup aceito com os service models KPM, RC, CCC e SLICE_STATS, e `xapp_kpm_moni` mostrando métricas.
- Roadmap: Etapa 8 (Near-RT RIC da O-RAN SC, para comparação com o FlexRIC), tabela de componentes prontos e próprios, e publicações planejadas.

### Alterado

- Guia da Etapa 1: tabela do que é automático (passos 1 a 4) e do que continua manual (subir RIC, gNB, UE e xApp, e validar).
- Guia da Etapa 1: o FlexRIC passa a ser compilado com gcc-12 (`CC=gcc-12 CXX=g++-12`, num diretório `build` limpo), porque o da tag `2026.w46` recusa o gcc-11 do Ubuntu 22.04. O OAI continua com o gcc-11. O caso foi incluído em `docs/troubleshooting.md`.
- Etapa 6: o lado Near-RT do A1 passa a ser uma terminação A1-P seguindo a OpenAPI do A1AP; O1 usa componentes prontos (O1 Adapter do OAI e OAM da O-RAN SC).
