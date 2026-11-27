# xApps

xApps próprias do openran-lab, em C, sob a [Apache License 2.0](../LICENSE). Rodam no Near-RT RIC FlexRIC da VM `ran` e são compiladas no provisionamento pela role `xapps`, que as coloca no `PATH` da VM.

| xApp | O que faz | Service model | Documentação |
| --- | --- | --- | --- |
| [`slice_kpm_monitor`](slice_kpm_monitor/) | Mede, a cada segundo, os PRBs e a vazão de cada slice (DL e UL) e escreve em texto e JSON Lines | E2SM-KPM, REPORT estilo 4, uma assinatura por slice | [Guia da Etapa 2](../docs/stages/02-multi-slice.md#xapp-de-monitoração-kpm-por-slice) e [decisão 21](../docs/decisions.md#21-xapp-de-monitoração-kpm-por-slice) |
| [`slice_quota_ctrl`](slice_quota_ctrl/) | Muda ou remove as cotas de PRB de um slice no gNB, sem reiniciá-lo | E2SM-RC, CONTROL estilo 2, ação 6 (Slice-level PRB quota) | [Guia da Etapa 2](../docs/stages/02-multi-slice.md#controle-de-cotas-pelo-e2sm-rc) e [decisão 25](../docs/decisions.md#25-controle-de-cotas-pelo-e2sm-rc-estilo-2) |

## Organização

Cada xApp separa a lógica da API do Near-RT RIC, para poder ser portada para o Near-RT RIC da O-RAN SC na Etapa 8 ([decisão 17](../docs/decisions.md#17-etapa-8-comparação-controlada-entre-near-rt-rics)):

```
<xapp>/
├── CMakeLists.txt     # aceita FLEXRIC_SRC_DIR e FLEXRIC_BUILD_DIR; gera um executável com o nome da pasta
├── src/
│   ├── <prefixo>_core.{h,c}   # núcleo: lógica da xApp, sem dependência de RIC
│   └── <prefixo>_flexric.c    # adaptador: única parte ligada à API do FlexRIC
└── tests/                     # testes do núcleo (ctest), sem RIC
```

## Compilação

Na VM, o provisionamento faz tudo. Para compilar à mão, por exemplo ao mexer no código direto na VM (o código copiado fica em `~/openran-lab-xapps/`):

```bash
FR=~/openairinterface5g/openair2/E2AP/flexric
CC=gcc-12 cmake -S ~/openran-lab-xapps/<xapp> -B ~/xapps-build/<xapp> -DFLEXRIC_SRC_DIR=$FR -DFLEXRIC_BUILD_DIR=$FR/build
cmake --build ~/xapps-build/<xapp> -j4 && ctest --test-dir ~/xapps-build/<xapp> --output-on-failure
```

Só o núcleo e os testes, sem FlexRIC (por exemplo, fora da VM): `cmake -S <xapp> -B build -DSQC_BUILD_XAPP=OFF` na `slice_quota_ctrl` ou `-DSKM_BUILD_XAPP=OFF` na `slice_kpm_monitor`.

Uma xApp nova entra em `xapps_list`, nos defaults da role `xapps`, e segue a mesma organização.
