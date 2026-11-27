# openran-lab

Laboratório OpenRAN reproduzível para estudo e pesquisa: gNB e UE do OpenAirInterface (OAI/Duranta) com RF simulado, core 5G OAI CN5G e Near-RT RIC FlexRIC. Tudo roda em VMs KVM criadas com Vagrant e provisionadas com Ansible.

O objetivo final é um cenário em que o consumidor de um slice acompanha dados da RAN num dashboard (via interface **Y1**) e, com um botão, pede mais recursos. O pedido percorre o caminho O-RAN (SMO → rApp → **A1** → xApp → **E2**) até o gNB.

> **Status:** em desenvolvimento. As Etapas 1 e 2 estão concluídas (releases `v0.1.0` e `v0.2.0`); a próxima é a Etapa 3, o caminho O-RAN completo. Veja o [roadmap](docs/roadmap.md).

## Status das etapas

| Etapa | Entrega | Status |
| --- | --- | --- |
| 1 | RAN + core + E2 (OAI gNB/nrUE em rfsim, OAI CN5G, FlexRIC) | Concluída (`v0.1.0`) |
| 2 | Multi-slice (dois slices, dois UEs, xApp de slicing) | Concluída (`v0.2.0`, [guia](docs/stages/02-multi-slice.md)) |
| 3 | Caminho O-RAN com FlexRIC (SMO, Non-RT RIC, rApp, terminação A1-P, O1) | Planejada |
| 4 | Interface Y1 (produtor e consumidor com mTLS) | Planejada |
| 5 | Observabilidade (OpenTelemetry, Prometheus, Grafana) | Planejada |
| 6 | Botão de ação para o consumidor e atalho de laboratório | Planejada |
| 7 | Automação completa e experimento final | Planejada |
| 8 | Near-RT RIC da O-RAN SC no lugar do FlexRIC, para comparação | Planejada |

## Arquitetura da Etapa 1

```
VM ran (Ubuntu 22.04)
├── OAI CN5G (Docker): AMF, SMF, UPF, ext-dn...
├── nr-softmodem (gNB, --rfsim) ── N2/N3 ──► CN5G
│        └── agente E2 ── E2 ──► nearRT-RIC (FlexRIC)
├── nr-uesoftmodem (UE, --rfsim) ── IQ via TCP ──► gNB
└── xApps (ex.: xapp_kpm_moni) ──► nearRT-RIC
```

A arquitetura final, com as VMs `obs` e `smo`, está em [docs/architecture.md](docs/architecture.md). As decisões técnicas (versões, compiladores, imagens fixadas, cuidados na subida do cenário), com o motivo de cada uma, estão em [docs/decisions.md](docs/decisions.md).

## Pré-requisitos

Antes de começar, o servidor precisa ter:

- Linux x86_64 com virtualização por hardware (Intel VT-x ou AMD-V) habilitada na BIOS;
- CPU com **AVX2**, que o OAI exige (AVX-512 é aproveitado se existir);
- recursos livres para a Etapa 1: **8 vCPUs, 16 GB de RAM e cerca de 100 GB de disco**;
- **KVM/libvirt, Vagrant, o plugin vagrant-libvirt e Ansible** instalados;
- o seu usuário nos grupos `libvirt` e `kvm`.

Versões com que o laboratório foi testado:

| Componente | Versão testada |
| --- | --- |
| Sistema do host | Ubuntu 24.04.5 LTS |
| QEMU/KVM | 8.2.2 |
| libvirt | 10.0.0 |
| Vagrant | 2.4.9 |
| vagrant-libvirt | 0.12.2 |
| Ansible | ansible-core 2.21.5 |

Problemas comuns na preparação do host estão em [docs/troubleshooting.md](docs/troubleshooting.md).

## Início rápido

```bash
git clone https://github.com/viniciussimao/openran-lab.git
cd openran-lab

cp config.example.yml config.yml   # ajuste CPU, RAM e rede ao seu servidor
./scripts/preflight.sh             # confere se o host atende aos requisitos

vagrant up                         # cria e provisiona as VMs habilitadas
vagrant ssh ran                    # entra na VM da RAN
```

O `vagrant up` compila o OAI e o FlexRIC e sobe o core 5G na VM `ran` (isso pode levar mais de uma hora). Subir o nearRT-RIC, o gNB, o UE e a xApp continua manual: siga o passo 5 do guia da Etapa 1 em [docs/stages/01-ran-core-e2.md](docs/stages/01-ran-core-e2.md).

## Configuração

O `Vagrantfile` lê o `config.yml`. Se ele não existir, usa o `config.example.yml`. O `config.yml` não vai para o Git, então cada servidor tem o seu.

| Chave | Para que serve |
| --- | --- |
| `box` | Box do Vagrant usada em todas as VMs |
| `management_network.name` | Nome da rede de gerenciamento criada pelo vagrant-libvirt |
| `management_network.address` | Sub-rede dessa rede, no formato CIDR |
| `vms.<nome>.enabled` | Liga ou desliga a VM (`ran`, `obs`, `smo`) |
| `vms.<nome>.cpus` / `memory_mb` | Recursos de cada VM |

### Nomes das VMs e rede

As VMs aparecem no libvirt com nomes fixos: `openran-ran`, `openran-obs` e `openran-smo`. Dentro do Vagrant, use só o nome curto (`vagrant ssh ran`).

Se a sub-rede padrão da rede de gerenciamento (`192.168.121.0/24`) já estiver em uso no seu servidor, troque `management_network` no `config.yml`. O `scripts/preflight.sh` avisa quando isso acontece.

## Versões fixas dos componentes

Para que o cenário seja reproduzível, as versões ficam fixas em [`ansible/group_vars/all.yml`](ansible/group_vars/all.yml):

| Componente | Versão |
| --- | --- |
| OAI RAN (Duranta) | tag `2026.w46` (commit `cb98db5b`) |
| FlexRIC | submódulo da tag do OAI (compilado com gcc-12) |
| OAI CN5G | v2.2.2 (imagens fixadas por digest) |
| E2AP / E2SM-KPM | v3 / v3.00 (iguais no OAI e no FlexRIC) |
| SO das VMs | Ubuntu 22.04 (`generic/ubuntu2204`) |

Qualquer atualização de versão é registrada no [CHANGELOG](CHANGELOG.md).

## Estrutura do repositório

```
openran-lab/
├── AGENTS.md                # instruções para agentes de IA (o CLAUDE.md importa este arquivo)
├── Vagrantfile              # VMs definidas a partir do config.yml
├── config.example.yml       # modelo de configuração por servidor
├── ansible/
│   ├── ansible.cfg
│   ├── site.yml             # playbook principal
│   ├── group_vars/all.yml   # versões fixas
│   └── roles/               # common, docker, oai_ran, flexric, oai_cn5g, xapps
├── configs/
│   ├── ue.conf              # configuração do nrUE (copiada para a VM)
│   └── ue-slice1.conf, ue-slice2.conf  # um UE por slice (Etapa 2)
├── scripts/
│   ├── preflight.sh         # verificação do host
│   ├── multi-ue-ns.sh       # namespaces dos nrUE (Etapa 2)
│   └── slice-prb-share.sh   # resumo das linhas [SLICE] do gNB (Etapa 2)
├── xapps/
│   ├── slice_kpm_monitor/   # xApp de KPM por slice (Etapa 2)
│   └── slice_quota_ctrl/    # xApp que muda as cotas por E2SM-RC (Etapa 2)
└── docs/
    ├── architecture.md
    ├── decisions.md         # registro das decisões técnicas
    ├── roadmap.md
    ├── troubleshooting.md
    └── stages/              # um guia por etapa
```

## Como contribuir

Veja o [CONTRIBUTING.md](CONTRIBUTING.md). Em resumo: nomes de arquivos e variáveis em inglês, documentação e comentários em português, commits no padrão Conventional Commits.

## Licença

O código deste repositório é distribuído sob a [Apache License 2.0](LICENSE).

O repositório **não redistribui** código de terceiros. O OAI RAN (Duranta), o OAI CN5G e o FlexRIC são baixados durante o provisionamento, a partir dos repositórios oficiais, e seguem as próprias licenças.
