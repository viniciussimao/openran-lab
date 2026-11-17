# Etapa 1: RAN + core + E2

**Status:** validada à mão no servidor de teste; passos 1 a 4 automatizados pelas roles `oai_ran`, `flexric` e `oai_cn5g` (validação das roles pendente).

**Objetivo:** uma rede 5G SA completa na VM `ran`, com o gNB conectado ao nearRT-RIC (FlexRIC) pela interface E2.

**Critério de aceite:** o UE pinga o ext-dn do core, e a xApp `xapp_kpm_moni` mostra métricas por UE.

## O que é automático e o que é manual

| Passo | Como |
| --- | --- |
| 1. Clonar o OAI na tag fixa e iniciar o submódulo do FlexRIC | Automático (role `oai_ran`) |
| 2. Compilar gNB e nrUE com o agente E2 | Automático (role `oai_ran`) |
| 3. Compilar e instalar o FlexRIC com gcc-12 | Automático (role `flexric`) |
| 4. Preparar o core 5G com as imagens fixadas por digest e subi-lo | Automático (role `oai_cn5g`) |
| Copiar `configs/ue.conf` para `~/ue.conf` | Automático (role `oai_ran`) |
| 5. Subir nearRT-RIC, gNB, UE e xApp | **Manual** |
| 6. Validar | **Manual** |

As roles são idempotentes: o OAI só é recompilado se o commit ou as versões de E2AP/KPM mudarem, e o FlexRIC só se o commit do submódulo, o compilador ou as opções do CMake mudarem. Cada build bem-sucedido deixa um registro em `cmake_targets/ran_build/build/.openran-lab-build` (OAI) e `/usr/local/lib/flexric/.openran-lab-build` (FlexRIC); apague o registro para forçar um novo build. Um build feito à mão, sem esse registro, é refeito uma vez no primeiro provisionamento.

Os passos 1 a 4 continuam descritos abaixo como referência e para quem quiser reproduzi-los à mão.

## Antes de começar

Suba a VM `ran`. O provisionamento configura DNS, disco, pacotes base e Docker, compila o OAI e o FlexRIC e sobe o core 5G. Os builds levam de dezenas de minutos a mais de uma hora, dependendo do servidor:

```bash
vagrant up ran
vagrant ssh ran
```

Os comandos a seguir rodam **dentro da VM**. Depois do `vagrant up`, siga direto para o passo 5.

## 1. Clonar o OAI na versão fixa

O FlexRIC vem como submódulo do OAI, na versão testada junto com a tag.

```bash
git clone https://github.com/duranta-project/openairinterface5g.git ~/openairinterface5g
cd ~/openairinterface5g
git checkout 2026.w46
git submodule update --init openair2/E2AP/flexric
```

## 2. Compilar o OAI com o agente E2

O OAI e o FlexRIC precisam usar as mesmas versões de E2AP e E2SM-KPM. O laboratório fixa E2AP v3 e KPM v3.00 nos dois lados.

```bash
cd ~/openairinterface5g/cmake_targets
./build_oai -I    # dependências; só na primeira vez
./build_oai -w SIMU --gNB --nrUE --build-e2 --ninja \
  --cmake-opt -DE2AP_VERSION=E2AP_V3 --cmake-opt -DKPM_VERSION=KPM_V3_00
```

## 3. Compilar e instalar o FlexRIC

`XAPP_MULTILANGUAGE=OFF` deixa só as xApps em C e dispensa o SWIG.

O FlexRIC desta tag não aceita o gcc-11, que é o compilador padrão do Ubuntu 22.04: o CMake para com `FlexRIC doesn't support gcc-11`. Por isso o FlexRIC é compilado com o gcc-12. O compilador padrão da VM continua sendo o gcc-11, porque o OAI (passo 2) compila normalmente com ele.

```bash
sudo apt install -y gcc-12 g++-12

cd ~/openairinterface5g/openair2/E2AP/flexric
rm -rf build    # o CMake guarda o compilador no cache; comece do zero
mkdir build && cd build
CC=gcc-12 CXX=g++-12 cmake -DXAPP_MULTILANGUAGE=OFF -DE2AP_VERSION=E2AP_V3 -DKPM_VERSION=KPM_V3_00 ..
make -j"$(nproc)"
sudo make install    # service models em /usr/local/lib/flexric/
```

## 4. Preparar o core 5G (OAI CN5G)

Os arquivos do core estão no próprio repositório do OAI:

```bash
cp -r ~/openairinterface5g/doc/tutorial_resources/oai-cn5g ~/oai-cn5g
cd ~/oai-cn5g
docker compose pull
```

O compose da tag `2026.w46` usa o OAI CN5G v2.2.2 (AMF, SMF, UPF, NRF, AUSF, UDM, UDR), mais `ims`, `oai-ext-dn` e `mysql`. A role `oai_cn5g` também fixa cada imagem por digest, no formato `imagem:tag@sha256:...`: a tag fica para leitura, o digest é o que vale. Ela gera, ao lado do compose, um `docker-compose.override.yaml` com as imagens de `oai_cn5g_image_digests` (`ansible/group_vars/all.yml`). O `docker compose` carrega esse override sozinho, então os comandos abaixo usam as imagens fixadas também quando rodados à mão. Para conferir:

```bash
cd ~/oai-cn5g && docker compose config --images    # todas devem terminar em @sha256:...
```

## 5. Subir o cenário

Use cinco terminais (`vagrant ssh ran` em cada um) ou janelas do `tmux`.

**Terminal 1: core 5G**

O provisionamento já deixa o core no ar. Depois de reiniciar a VM, ou se ele tiver sido derrubado, suba de novo:

```bash
cd ~/oai-cn5g && docker compose up -d
docker compose ps    # aguarde todos os containers ficarem "healthy"
```

**Terminal 2: nearRT-RIC**

```bash
cd ~/openairinterface5g/openair2/E2AP/flexric
./build/examples/ric/nearRT-RIC
```

**Terminal 3: gNB com agente E2**

```bash
cd ~/openairinterface5g/cmake_targets/ran_build/build
sudo ./nr-softmodem -O ../../../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band78.fr1.106PRB.usrpb210.conf \
  --gNBs.[0].min_rxtxtime 6 --rfsim \
  --e2_agent.near_ric_ip_addr "127.0.0.1" --e2_agent.sm_dir "/usr/local/lib/flexric/"
```

No terminal 2 deve aparecer o E2 Setup do gNB.

**Terminal 4: UE**

```bash
cd ~/openairinterface5g/cmake_targets/ran_build/build
sudo ./nr-uesoftmodem -r 106 --numerology 1 --band 78 -C 3619200000 --rfsim \
  --uicc0.imsi 001010000000001 --rfsimulator.[0].serveraddr 127.0.0.1
```

**Terminal 5: xApp de KPM**

```bash
cd ~/openairinterface5g/openair2/E2AP/flexric
XAPP_DURATION=60 ./build/examples/xApp/c/monitor/xapp_kpm_moni
```

## 6. Validar

```bash
ping -I oaitun_ue1 192.168.70.135    # UE até o ext-dn do core
```

- [ ] `ping` do UE até o ext-dn responde
- [ ] E2 Setup aparece no log do nearRT-RIC
- [ ] `xapp_kpm_moni` imprime métricas do UE

## Referências

- [OAI Code (repositórios oficiais)](https://openairinterface.org/oai-code/)
- README do E2AP no repositório do OAI: `openair2/E2AP/README.md`
- Tutorial OAI NR SA com nrUE no repositório do OAI: `doc/NR_SA_Tutorial_OAI_nrUE.md`
