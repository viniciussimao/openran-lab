# AGENTS.md

Instruções para agentes de IA que trabalham neste repositório. Leia este arquivo inteiro antes de qualquer tarefa.

## O que é o projeto

O `openran-lab` é um laboratório OpenRAN reproduzível e público: gNB e nrUE do OAI (projeto Duranta) com RF simulado, core 5G OAI CN5G e Near-RT RIC FlexRIC, em VMs KVM criadas pelo Vagrant (libvirt) e provisionadas pelo Ansible. O objetivo final é um consumidor de slice que acompanha dados da RAN num dashboard (Y1 + OpenTelemetry + Grafana) e pede mais banda pelo caminho O-RAN (SMO → rApp → A1 → xApp → E2 → gNB).

O estado de cada etapa está na tabela "Status das etapas" do [README](README.md).

## Onde está cada coisa

| Arquivo | Conteúdo |
| --- | --- |
| [README.md](README.md) | Visão geral, status das etapas, pré-requisitos, versões fixas e estrutura do repositório |
| [docs/roadmap.md](docs/roadmap.md) | Etapas, critérios de aceite, releases, componentes prontos e próprios, experimento final e riscos |
| [docs/architecture.md](docs/architecture.md) | Arquitetura final, interfaces O-RAN e VMs |
| [docs/decisions.md](docs/decisions.md) | Decisões técnicas, com contexto, motivo e consequências. Consulte antes de mudar versões, roles ou a subida do cenário |
| [docs/stages/](docs/stages/) | Guia de cada etapa, passo a passo |
| [docs/troubleshooting.md](docs/troubleshooting.md) | Sintomas, causas e soluções já encontrados |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Fluxo de contribuição e padrões de código |
| [CHANGELOG.md](CHANGELOG.md) | Mudanças por versão |

## Convenções obrigatórias

| Item | Idioma |
| --- | --- |
| Nomes de arquivos, pastas, variáveis, roles e funções | Inglês |
| `README.md`, `docs/`, comentários no código e nomes das tasks Ansible | Português |
| Mensagens de commit | Inglês, [Conventional Commits](https://www.conventionalcommits.org/) (`feat:`, `fix:`, `docs:`, `chore:`) |

- **Ansible:** módulos com nome completo (`ansible.builtin.*`), toda task com `name` e variáveis de role com o prefixo da role (`common_`, `docker_`, `oai_ran_`...).
- **Versões fixas** em `ansible/group_vars/all.yml`. Nunca use `latest` nem branches móveis; imagens de container são fixadas por digest.
- **Nada que varie entre servidores fica fixo no código:** vai para o `config.yml` (modelo em `config.example.yml`).
- **Sem código de terceiros:** OAI, CN5G e FlexRIC são baixados no provisionamento.
- **Sem segredos**, chaves ou certificados no Git.
- **Finais de linha LF** (`.gitattributes`). Nunca introduza CRLF.
- **Manual primeiro, automação depois:** um passo só vira role Ansible depois de validado à mão num servidor e documentado em `docs/stages/`.

## Fluxo de trabalho

- O `main` é protegido por um ruleset no GitHub: não aceita push direto, push forçado nem exclusão. Nunca faça commit nem push no `main`.
- Toda mudança, inclusive de documentação, vai num branch `feat/`, `fix/` ou `docs/` com pull request. Envie cada commit para o branch logo depois de commitar, para o CI rodar.
- O merge só é liberado quando os dois jobs do CI passam (`yamllint e ansible-lint` e `shellcheck e Vagrantfile`). Se o CI falhar, corrija no mesmo branch.
- Registre cada mudança no `CHANGELOG.md`, na seção "Não publicado".
- Mantenha nos commits a linha `Co-Authored-By` do agente que os escreveu.

## Ao concluir uma tarefa ou etapa

1. Atualize a documentação afetada (`README.md`, `docs/`) e, se a tarefa tomou ou mudou uma decisão técnica, `docs/decisions.md`.
2. Registre a mudança no `CHANGELOG.md`, na seção "Não publicado".
3. Ao fechar uma etapa: atualize a tabela de status do README, mova as entradas do CHANGELOG para a nova versão e prepare a tag da release (`v0.1.0` = Etapa 1, `v0.2.0` = Etapa 2, até `v1.0.0`; veja o [roadmap](docs/roadmap.md)).
4. Diga quais comandos rodar no servidor de teste para validar a mudança.

## Verificações

Antes de commitar, rode as verificações do CI quando as ferramentas estiverem disponíveis:

```bash
yamllint .
ansible-lint ansible/
shellcheck scripts/*.sh
ruby -c Vagrantfile
```
