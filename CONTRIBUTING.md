# Como contribuir

Obrigado pelo interesse em contribuir com o openran-lab. Este guia resume as convenções do projeto.

## Convenções de idioma

| Item | Idioma |
| --- | --- |
| Nomes de arquivos, pastas, variáveis, roles e funções | Inglês |
| README, documentação em `docs/` e comentários no código | Português |
| Mensagens de commit | Inglês, no padrão Conventional Commits |

## Fluxo de trabalho

1. Abra uma issue descrevendo o problema ou a proposta.
2. Crie um branch a partir do `main`: `feat/<assunto>`, `fix/<assunto>` ou `docs/<assunto>`.
3. Faça commits pequenos no padrão [Conventional Commits](https://www.conventionalcommits.org/pt-br/v1.0.0/):
   - `feat: add flexric role`
   - `fix: wait for dns before apt`
   - `docs: describe stage 2 slices`
4. Teste num servidor real: `./scripts/preflight.sh`, `vagrant up` e o critério de aceite da etapa.
5. Abra um pull request preenchendo o modelo, com a saída relevante dos testes.
6. Se o CI falhar, corrija no mesmo branch: os novos commits entram no mesmo PR.

O branch `main` é protegido por um ruleset no GitHub:

- não aceita push direto, push forçado nem exclusão;
- toda mudança, inclusive de documentação, entra por pull request (não é exigida aprovação, porque o projeto tem um só mantenedor);
- o merge só é liberado quando os dois jobs do CI passam: `yamllint e ansible-lint` e `shellcheck e Vagrantfile`.

Nunca faça commit no `main`. Nos branches de trabalho, envie os commits para o PR assim que os fizer, para que o CI rode e o servidor de teste possa usar o branch.

## Padrões de código

- **Ansible:** módulos com nome completo (`ansible.builtin.apt`), toda task com `name` em português e variáveis de role com o prefixo da role (`common_`, `docker_`).
- **Versões:** nunca use `latest` nem branches móveis. Versões novas entram em `ansible/group_vars/all.yml` e no `CHANGELOG.md`.
- **Shell:** `bash` com `set -u` no mínimo, e sem avisos do `shellcheck`.
- **Configuração por servidor:** valores que mudam de um host para outro vão para o `config.yml`, nunca fixos no código.

## Verificações locais

As mesmas verificações do CI podem rodar na sua máquina:

```bash
pip install yamllint ansible-lint
yamllint .
ansible-lint ansible/
shellcheck scripts/*.sh
ruby -c Vagrantfile
```

## Releases

Cada etapa concluída gera uma release (`v0.1.0` = Etapa 1, e assim por diante). A entrada correspondente no `CHANGELOG.md` lista as versões testadas.
