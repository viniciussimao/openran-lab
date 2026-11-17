# Changelog

Todas as mudanças relevantes deste projeto são registradas aqui.

O formato segue o [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/), e o projeto adota o [Versionamento Semântico](https://semver.org/lang/pt-BR/). Cada etapa do [roadmap](docs/roadmap.md) gera uma release.

## [Não publicado]

### Adicionado

- Estrutura inicial do repositório: README, licença Apache-2.0, guia de contribuição e política de segurança.
- `Vagrantfile` configurável por `config.yml` (VMs, recursos e rede de gerenciamento), com VMs nomeadas `openran-ran`, `openran-obs` e `openran-smo`.
- Roles Ansible `common` (DNS, disco e pacotes base) e `docker`, validadas no servidor de teste em 2026-11-17: `vagrant up ran` terminou com `failed=0`, o disco da VM foi estendido de 62 para 124 GB, o Docker 29.10.0 foi instalado e o DNS ficou em `192.168.121.1` (gateway da rede de gerenciamento).
- `scripts/preflight.sh` para verificar o host antes do `vagrant up`.
- Documentação: arquitetura, roadmap, solução de problemas e guia manual da Etapa 1.
- CI com yamllint, ansible-lint, shellcheck e verificação de sintaxe do `Vagrantfile`.
- Roadmap: Etapa 8 (Near-RT RIC da O-RAN SC, para comparação com o FlexRIC), tabela de componentes prontos e próprios, e publicações planejadas.

### Alterado

- Etapa 6: o lado Near-RT do A1 passa a ser uma terminação A1-P seguindo a OpenAPI do A1AP; O1 usa componentes prontos (O1 Adapter do OAI e OAM da O-RAN SC).
