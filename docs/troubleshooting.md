# Solução de problemas

Problemas encontrados durante a montagem do laboratório e como resolvê-los.

## Host

| Sintoma | Causa | Solução |
| --- | --- | --- |
| `libicu-dev : Depende: libicu74 (= ...)` ou `nfs-common : Depende: libnfsidmap1` ao instalar dependências | Repositório `noble-updates` ausente no `ubuntu.sources` | Incluir `noble-updates` (e `noble-backports`) na linha `Suites:` de `/etc/apt/sources.list.d/ubuntu.sources` e rodar `sudo apt update` |
| `NO_PUBKEY FC9CA96ACA026560` no `apt update` | Chave antiga da HashiCorp não foi substituída | Apagar `/usr/share/keyrings/hashicorp-archive-keyring.gpg` e baixar de novo com `gpg --dearmor --batch --yes` |
| `vagrant plugin install vagrant-libvirt` falha procurando o libvirt | Bibliotecas de desenvolvimento ausentes | Instalar `libvirt-dev ruby-dev libxml2-dev libxslt1-dev zlib1g-dev pkg-config build-essential` |
| `[fog][WARNING] Unrecognized arguments: libvirt_ip_command` | Incompatibilidade de versão entre vagrant-libvirt e fog-libvirt | Ignorar: não afeta o funcionamento |
| Rede `default` ou `vagrant-libvirt` não sobe | Rede inativa ou sub-rede já em uso no host | `virsh net-list --all`; se a sub-rede estiver ocupada, troque `management_network` no `config.yml` |

## VMs

| Sintoma | Causa | Solução |
| --- | --- | --- |
| VM sem DNS; `resolvectl query` mostra `DNSSEC validation failed: missing-key` | As boxes `generic/*` vêm com DNS públicos fixos e DNSSEC ligado | Já tratado pela role `common`. Para corrigir à mão: DNS = gateway da rede de gerenciamento, `DNSSEC=no`, `DNSOverTLS=no` |
| `Cannot call Open vSwitch: ovsdb-server.service is not running` no provisionamento | Aviso do `netplan apply` | Ignorar |
| Pouco espaço em `/` dentro da VM | O LVM da box não usa o disco inteiro | Já tratado pela role `common` (`lvextend -r -l +100%FREE`) |
| OAI lento ou sem otimizações de CPU | VM sem AVX2 | Confirmar `cpu_mode = "host-passthrough"` e `grep avx2 /proc/cpuinfo` na VM |
