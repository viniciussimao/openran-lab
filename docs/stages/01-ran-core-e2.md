# Etapa 1: RAN + core + E2

**Status:** em andamento (guia manual). Os passos validados aqui viram as roles `oai_cn5g`, `oai_ran` e `flexric`.

**Objetivo:** uma rede 5G SA completa na VM `ran`, com o gNB conectado ao nearRT-RIC (FlexRIC) pela interface E2.

**Critério de aceite:** o UE pinga o ext-dn do core, e a xApp `xapp_kpm_moni` mostra métricas por UE.

## Antes de começar

Suba a VM `ran`. O provisionamento já configura DNS, disco, pacotes base e Docker:

```bash
vagrant up ran
vagrant ssh ran
```

Os comandos a seguir rodam **dentro da VM**.

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

```bash
cd ~/openairinterface5g/openair2/E2AP/flexric
mkdir -p build && cd build
cmake -DXAPP_MULTILANGUAGE=OFF -DE2AP_VERSION=E2AP_V3 -DKPM_VERSION=KPM_V3_00 ..
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

## 5. Subir o cenário

Use cinco terminais (`vagrant ssh ran` em cada um) ou janelas do `tmux`.

**Terminal 1: core 5G**

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
