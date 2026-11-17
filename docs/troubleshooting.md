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
| `vagrant provision` para em "Conferir se o clone está no commit fixo", com o clone num commit diferente de `oai_commit` | `oai_commit` com o hash do objeto de uma tag anotada, e não do commit da tag | Obter o commit com `git ls-remote <url> 'refs/tags/<tag>^{}'` ou, num clone, `git rev-parse '<tag>^{commit}'`, e corrigir `oai_commit` em `ansible/group_vars/all.yml`. O `refs/tags/<tag>` de uma tag anotada não serve |

## Compilação

| Sintoma | Causa | Solução |
| --- | --- | --- |
| `FlexRIC doesn't support gcc-11` no `cmake` do FlexRIC | O FlexRIC da tag `2026.w46` recusa o gcc-11, padrão do Ubuntu 22.04 | Instalar `gcc-12 g++-12`, apagar o diretório `build` e rodar o `cmake` com `CC=gcc-12 CXX=g++-12` (passo 3 do [guia da Etapa 1](stages/01-ran-core-e2.md)). Não troque o compilador padrão: o OAI compila com o gcc-11 |

## Execução

| Sintoma | Causa | Solução |
| --- | --- | --- |
| nearRT-RIC aborta com `Not valid path for the SM plugins. Check the path (/usr/local/lib/flexric/.openran-lab-build)!` e `load_plugin_ric: Assertion handle != NULL failed` | O nearRT-RIC carrega como plugin todo arquivo de `/usr/local/lib/flexric/`, e as primeiras versões da role `flexric` gravavam ali o registro de idempotência. O agente E2 do gNB (`--e2_agent.sm_dir`) lê a mesma pasta e falharia igual | Rodar `vagrant provision ran`: a role agora grava os registros em `/var/lib/openran-lab/`, apaga o arquivo antigo e confere se a pasta só tem `.so`. À mão: `sudo rm /usr/local/lib/flexric/.openran-lab-build`. Nunca coloque outros arquivos nessa pasta |
| nrUE registra no core, mas o log mostra `[SIM] no PDU sessions to request configured` e a interface `oaitun_ue1` não é criada | UE iniciado sem arquivo de configuração (só com `--uicc0.imsi`): sem `pdu_sessions`, ele não pede sessão PDU | Rodar o UE com `-O ~/ue.conf` (copiado de `configs/ue.conf` pelo provisionamento) e sem `--uicc0.imsi`. Não use o `ue.conf` do OAI: ele tem outro IMSI e um modelo de canal de satélite LEO. Veja o passo 5 do [guia da Etapa 1](stages/01-ran-core-e2.md) |
| UE registra e envia o `PduSessionEstablishRequest`, mas a sessão não sobe; a AMF loga `NFDiscovery, SMF Info: Addr , Port 8080` e depois `SMF Selection, no SMF candidate is available` | O `oai-smf` (v2.2.2) travou: o log dele para por completo, sem erro, junto com os heartbeats para o NRF e o PFCP com o UPF. O container continua `healthy`, porque o healthcheck da imagem não detecta esse estado | Reiniciar o core (`cd ~/oai-cn5g && docker compose down && docker compose up -d --wait`) e depois o gNB e o UE. Para confirmar o diagnóstico, compare a hora da última linha de `docker compose logs oai-smf` com a de `oai-amf`; não confie no estado `healthy` |
